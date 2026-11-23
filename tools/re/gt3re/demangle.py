"""Demangler for GCC 2.9x type names and (best effort) function symbols.

Only type names are needed for RTTI, e.g.
  10GameObject                          -> GameObject
  Q29gt2k_menu12OperatorList            -> gt2k_menu::OperatorList
  t4List1ZQ29gt2k_menu14OperatorHolder  -> List<gt2k_menu::OperatorHolder>
"""

BUILTIN = {"v": "void", "c": "char", "s": "short", "i": "int", "l": "long", "x": "long long",
           "f": "float", "d": "double", "r": "long double", "b": "bool", "w": "wchar_t", "e": "..."}


class _P:
    def __init__(self, s):
        self.s = s
        self.i = 0

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def take(self, n=1):
        r = self.s[self.i:self.i + n]
        self.i += n
        return r

    def number(self):
        j = self.i
        while self.i < len(self.s) and self.s[self.i].isdigit():
            self.i += 1
        if j == self.i:
            raise ValueError("number expected")
        return int(self.s[j:self.i])


def _name(p):
    c = p.peek()
    if c == "t":
        p.take()
        base = _name(p)
        n = p.number() if p.peek().isdigit() else 0
        args = []
        for _ in range(n):
            if p.peek() == "Z":
                p.take()
                args.append(_type(p))
            else:
                t = _type(p)
                neg = False
                if p.peek() == "m":
                    p.take()
                    neg = True
                if p.peek().isdigit():
                    v = p.number()
                    args.append(str(-v if neg else v))
                else:
                    args.append(t)
        return "%s<%s>" % (base, ", ".join(args))
    n = p.number()
    return p.take(n)


def _qualified(p):
    p.take()  # Q
    if p.peek() == "_":
        p.take()
        n = p.number()
        if p.peek() == "_":
            p.take()
    else:
        n = int(p.take())
    return "::".join(_name(p) for _ in range(n))


def _type(p):
    c = p.peek()
    if c == "P":
        p.take()
        return _type(p) + "*"
    if c == "R":
        p.take()
        return _type(p) + "&"
    if c == "C":
        p.take()
        return "const " + _type(p)
    if c == "V":
        p.take()
        return "volatile " + _type(p)
    if c == "U":
        p.take()
        return "unsigned " + _type(p)
    if c == "S":
        p.take()
        return "signed " + _type(p)
    if c == "Q":
        return _qualified(p)
    if c == "t" or c.isdigit():
        return _name(p)
    if c == "A":  # array: A<n>_<type>
        p.take()
        n = p.number()
        p.take()  # _
        return "%s[%d]" % (_type(p), n)
    if c == "F":  # function type: F<args>_<ret>
        p.take()
        args = []
        while p.peek() and p.peek() != "_":
            args.append(_type(p))
        p.take()
        ret = _type(p)
        return "%s (*)(%s)" % (ret, ", ".join(args))
    if c in BUILTIN:
        p.take()
        return BUILTIN[c]
    raise ValueError("bad type at %d in %s" % (p.i, p.s))


def demangle_type(s):
    """Demangle a GCC 2.9x type name; returns the input on failure."""
    try:
        p = _P(s)
        r = _type(p)
        if p.i != len(s):
            return s
        return r
    except (ValueError, IndexError):
        return s


def looks_like_type_name(s):
    """Heuristic: a mangled GCC 2.9x class/type name as used in RTTI."""
    if not s or len(s) < 2 or len(s) > 400 or not s.isascii():
        return False
    if not all(c.isalnum() or c == "_" for c in s):
        return False
    c = s[0]
    if c.isdigit() or c in "QtPRCU":
        d = demangle_type(s)
        return d != s or (c.isdigit() and _len_ok(s))
    return False


def _len_ok(s):
    p = _P(s)
    try:
        n = p.number()
    except ValueError:
        return False
    return len(s) == p.i + n
