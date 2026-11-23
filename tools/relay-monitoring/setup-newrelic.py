#!/usr/bin/env python3
"""Creates or updates the relay's alerting in New Relic: the "GT3R relay" policy, its
conditions and an email workflow. Matches everything by name and updates in place, so
edit CONDITIONS and re-run.

    NEW_RELIC_API_KEY=NRAK-... NEW_RELIC_ACCOUNT_ID=<id> ALERT_EMAIL=you@example.com python3 tools/relay-monitoring/setup-newrelic.py

NEW_RELIC_API_KEY is a User API key (NRAK-), used from your machine only; it never goes
on the server. The account is shared with other projects, so every condition is limited
to the gt3r-relay entity. Standard library only.
"""
import json
import os
import sys
import urllib.request

ACCOUNT_ID = int(os.environ["NEW_RELIC_ACCOUNT_ID"])
API_KEY = os.environ["NEW_RELIC_API_KEY"]
ALERT_EMAIL = os.environ["ALERT_EMAIL"]

POLICY = "GT3R relay"
NOTIFY = "GT3R relay email"
HOST = "entityName = 'gt3r-relay'"
# Flex samples carry entityKey from the first minute; entityName only after the agent
# has decorated the host, which can lag a restart.
RELAY = "FROM GT3RelaySample WHERE entityKey = 'gt3r-relay'"


def gql(query, **variables):
    req = urllib.request.Request(
        "https://api.newrelic.com/graphql",
        data=json.dumps({"query": query, "variables": variables}).encode(),
        headers={"Content-Type": "application/json", "API-Key": API_KEY},
    )
    body = json.load(urllib.request.urlopen(req))
    if body.get("errors"):
        sys.exit(f"NerdGraph error: {json.dumps(body['errors'])}")
    return body["data"]


def check(result, what):
    """Mutation payloads report problems in an errors/error field, not as GraphQL errors."""
    errs = result.get("errors") or ([result["error"]] if result.get("error") else [])
    if errs:
        sys.exit(f"{what}: {json.dumps(errs)}")
    return result


def condition(name, query, threshold, *, description, op="ABOVE", window=60, duration=None,
              occurrences="ALL", warning=None, lost_signal=None):
    duration = duration or window
    terms = [{"priority": "CRITICAL", "operator": op, "threshold": threshold,
              "thresholdDuration": duration, "thresholdOccurrences": occurrences}]
    if warning is not None:
        terms.append(dict(terms[0], priority="WARNING", threshold=warning))
    c = {"name": name, "description": description, "enabled": True, "nrql": {"query": query},
         "terms": terms, "violationTimeLimitSeconds": 86400,
         "signal": {"aggregationWindow": window, "aggregationMethod": "EVENT_FLOW", "aggregationDelay": 120}}
    if lost_signal:
        c["expiration"] = {"expirationDuration": lost_signal, "openViolationOnExpiration": True,
                           "closeViolationsOnExpiration": False}
    return c


CONDITIONS = [
    # Host
    condition("Relay host not reporting", f"SELECT count(*) FROM SystemSample WHERE {HOST}", 1,
              op="BELOW", lost_signal=600,
              description="The relay host sent no metrics for 10 minutes: down, agent stopped, or cut off."),
    condition("Relay host disk filling", f"SELECT max(diskUsedPercent) FROM StorageSample WHERE {HOST} FACET mountPoint", 90,
              warning=80, window=300, duration=900, description="A filesystem on the relay host is above 80/90%."),
    condition("Relay host memory", f"SELECT max(memoryUsedPercent) FROM SystemSample WHERE {HOST}", 95,
              window=300, duration=900, description="Memory above 95% for 15 minutes (1 GB box)."),
    # The relay (relay-health.py, every minute)
    condition("Relay down", f"SELECT max(relayActive) {RELAY}", 1, op="BELOW", duration=180,
              description="gt3r-relay.service isn't active for 3 minutes; online play is unavailable."),
    condition("Relay not answering", f"SELECT max(probeOk) {RELAY}", 1, op="BELOW", duration=300,
              description="The relay hasn't answered the local UDP probe for 5 minutes (running but stuck, or the socket is gone)."),
    condition("Relay restarting", f"SELECT max(relayRestarts) - min(relayRestarts) {RELAY}", 2,
              window=1800, occurrences="AT_LEAST_ONCE", description="systemd restarted the relay more than twice in 30 minutes."),
    condition("Relay memory near its cap", f"SELECT max(relayMemoryBytes) {RELAY}", 200e6,
              window=300, duration=900, description="The relay uses over 200 MB of its 256 MB MemoryMax for 15 minutes."),
    condition("Link slots nearly full", f"SELECT max(linksUsed * 100 / linksCapacity) {RELAY}", 80,
              window=300, duration=600, description="Over 80% of the relay's link slots in use for 10 minutes; raise --relay-links or the plan."),
    condition("Lobbies waiting for slots", f"SELECT max(queued) {RELAY}", 0, window=300, duration=600,
              description="Lobbies have been queued for link slots for 10 minutes: players see 'Relay busy'."),
    # Outside reachability. Nothing here can send UDP from the internet (New Relic's free
    # ping monitors are HTTP), but every game launch sends a version check, so a silent
    # relay while the process is fine means players can't reach it: DNS, firewall, routing.
    # Since 2026-10-21 there has been no hour without one (~120/hour); loosen this if
    # traffic falls back to the earlier dozens a day.
    condition("No players reaching the relay", f"SELECT sum(versionChecks) {RELAY}", 1, op="BELOW",
              window=3600, duration=7200,
              description="No game has reached the relay for 2 hours although it's up locally. Check DNS, the firewall and UDP 47631."),
    condition("Relay health check silent", f"SELECT count(*) {RELAY}", 1, op="BELOW", window=300,
              lost_signal=900, description="relay-health.py hasn't reported for 15 minutes, so the relay checks are blind."),
    # Samples can keep arriving (the UDP probe) after the stats line stops being read, which is
    # what happened when the relay moved to journal namespace gt3r-relay. 9999 means "no line".
    condition("Relay stats line missing", f"SELECT max(statsAgeSeconds) {RELAY}", 180,
              window=300, duration=300,
              description="The newest stats: line is older than 3 minutes (or missing: statsAgeSeconds 9999). "
                          "Game-stat fields are absent. The health script must read journalctl --namespace=gt3r-relay."),
    condition("Relay host failed unit", f"SELECT min(failedUnitCount) {RELAY}", 0, window=900,
              occurrences="AT_LEAST_ONCE", description="A systemd unit on the relay host is failed. See failedUnits in GT3RelaySample."),
    condition("Relay host reboot overdue", f"SELECT min(rebootPendingDays) {RELAY}", 14, window=3600,
              occurrences="AT_LEAST_ONCE", description="An installed kernel or library update has waited over two weeks for a reboot."),
]


def ensure_policy():
    found = gql(
        """query($a: Int!, $n: String!) { actor { account(id: $a) { alerts {
             policiesSearch(searchCriteria: {name: $n}) { policies { id name } } } } } }""",
        a=ACCOUNT_ID, n=POLICY,
    )["actor"]["account"]["alerts"]["policiesSearch"]["policies"]
    for p in found:
        if p["name"] == POLICY:
            return p["id"]
    return gql(
        """mutation($a: Int!, $p: AlertsPolicyInput!) { alertsPolicyCreate(accountId: $a, policy: $p) { id } }""",
        a=ACCOUNT_ID, p={"name": POLICY, "incidentPreference": "PER_CONDITION_AND_TARGET"},
    )["alertsPolicyCreate"]["id"]


def ensure_conditions(policy_id):
    existing = {
        c["name"]: c["id"]
        for c in gql(
            """query($a: Int!, $p: ID!) { actor { account(id: $a) { alerts {
                 nrqlConditionsSearch(searchCriteria: {policyId: $p}) { nrqlConditions { id name } } } } } }""",
            a=ACCOUNT_ID, p=policy_id,
        )["actor"]["account"]["alerts"]["nrqlConditionsSearch"]["nrqlConditions"]
    }
    for c in CONDITIONS:
        if c["name"] in existing:
            gql("""mutation($a: Int!, $id: ID!, $c: AlertsNrqlConditionUpdateStaticInput!) {
                     alertsNrqlConditionStaticUpdate(accountId: $a, id: $id, condition: $c) { id } }""",
                a=ACCOUNT_ID, id=existing[c["name"]], c=c)
            print(f"condition: updated  {c['name']}")
        else:
            gql("""mutation($a: Int!, $p: ID!, $c: AlertsNrqlConditionStaticInput!) {
                     alertsNrqlConditionStaticCreate(accountId: $a, policyId: $p, condition: $c) { id } }""",
                a=ACCOUNT_ID, p=policy_id, c=c)
            print(f"condition: created  {c['name']}")


ERR = """error { __typename
  ... on AiNotificationsResponseError { description type }
  ... on AiNotificationsDataValidationError { details fields { field message } }
  ... on AiNotificationsConstraintsError { constraints { name dependencies } } }"""


def ensure_notifications(policy_id):
    acct = gql(
        """query($a: Int!, $n: String!) { actor { account(id: $a) {
             aiNotifications {
               destinations(filters: {name: $n}) { entities { id name } }
               channels(filters: {name: $n}) { entities { id name } } }
             aiWorkflows { workflows(filters: {name: $n}) { entities { id name } } } } } }""",
        a=ACCOUNT_ID, n=NOTIFY,
    )["actor"]["account"]
    dests = [d for d in acct["aiNotifications"]["destinations"]["entities"] if d["name"] == NOTIFY]
    if dests:
        dest_id = dests[0]["id"]
    else:
        dest_id = check(gql(
            f"""mutation($a: Int!, $d: AiNotificationsDestinationInput!) {{
                  aiNotificationsCreateDestination(accountId: $a, destination: $d) {{ destination {{ id }} {ERR} }} }}""",
            a=ACCOUNT_ID, d={"type": "EMAIL", "name": NOTIFY, "properties": [{"key": "email", "value": ALERT_EMAIL}]},
        )["aiNotificationsCreateDestination"], "destination")["destination"]["id"]
        print("destination: created")
    chans = [c for c in acct["aiNotifications"]["channels"]["entities"] if c["name"] == NOTIFY]
    if chans:
        chan_id = chans[0]["id"]
    else:
        chan_id = check(gql(
            f"""mutation($a: Int!, $c: AiNotificationsChannelInput!) {{
                  aiNotificationsCreateChannel(accountId: $a, channel: $c) {{ channel {{ id }} {ERR} }} }}""",
            a=ACCOUNT_ID, c={"type": "EMAIL", "name": NOTIFY, "destinationId": dest_id, "product": "IINT",
                             "properties": [{"key": "subject", "value": "{{ issueTitle }}"}]},
        )["aiNotificationsCreateChannel"], "channel")["channel"]["id"]
        print("channel: created")
    if any(w["name"] == NOTIFY for w in acct["aiWorkflows"]["workflows"]["entities"]):
        print("workflow: exists")
        return
    check(gql(
        """mutation($a: Int!, $w: AiWorkflowsCreateWorkflowInput!) {
             aiWorkflowsCreateWorkflow(accountId: $a, createWorkflowData: $w) { workflow { id } errors { description type } } }""",
        a=ACCOUNT_ID,
        w={"name": NOTIFY, "workflowEnabled": True, "destinationsEnabled": True, "enrichmentsEnabled": True,
           "mutingRulesHandling": "NOTIFY_ALL_ISSUES",
           "issuesFilter": {"name": POLICY, "type": "FILTER",
                            "predicates": [{"attribute": "labels.policyIds", "operator": "EXACTLY_MATCHES",
                                            "values": [str(policy_id)]}]},
           "destinationConfigurations": [{"channelId": chan_id, "notificationTriggers": ["ACTIVATED", "CLOSED"]}]},
    )["aiWorkflowsCreateWorkflow"], "workflow")
    print("workflow: created")


if __name__ == "__main__":
    policy_id = ensure_policy()
    print(f"policy: {POLICY} ({policy_id})")
    ensure_conditions(policy_id)
    ensure_notifications(policy_id)
