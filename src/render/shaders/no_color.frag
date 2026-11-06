#version 450
// Writes no colour, for the draws that only set the depth (PipelineKey::depth_reset): a full-screen
// quad through model.frag costs a phone GPU more than a millisecond.
void main() {}
