#include <linux/ptrace.h>
#include <linux/blkdev.h>
#include "../../src/cc/bpf_helpers.h"
struct Request { u64 rq; };
struct Time { u64 start; };
BPF_TABLE("hash", struct Request, struct Time, requests, 1024);
#define SLOTS 100
BPF_TABLE("array", u32, u64, latency, SLOTS);

static u32 log2(u32 v) {
  u32 r, shift;

  r = (v > 0xFFFF) << 4; v >>= r;
  shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
  shift = (v > 0xF) << 2; v >>= shift; r |= shift;
  shift = (v > 0x3) << 1; v >>= shift; r |= shift;
  r |= (v >> 1);
  return r;
}

static u32 log2l(u64 v) {
  u32 hi = v >> 32;
  if (hi)
    return log2(hi) + 32;
  else
    return log2(v);
}

BPF_EXPORT(probe_blk_start_request)
int probe_blk_start_request(struct pt_regs *ctx) {
  struct Request rq = {.rq = ctx->di};
  struct Time tm = {.start = bpf_ktime_get_ns()};
  requests.put(&rq, &tm);
  return 0;
}

BPF_EXPORT(probe_blk_update_request)
int probe_blk_update_request(struct pt_regs *ctx) {
  struct Request rq = {.rq = ctx->di};
  struct Time *tm = requests.get(&rq);
  if (!tm) return 0;
  u64 delta = bpf_ktime_get_ns() - tm->start;
  requests.delete(&rq);
  u64 lg = log2l(delta);
  u64 base = 1ull << lg;
  u32 index = (lg * 64 + (delta - base) * 64 / base) * 3 / 64;
  if (index >= SLOTS)
    index = SLOTS - 1;
  __sync_fetch_and_add(&latency.data[(u64)&index], 1);
  return 0;
}
