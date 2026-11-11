// Memo storage: one multi-line string in NVS, lines separated by
// '\n'. A single NVS string value is limited to about 1984 bytes, so
// memos are capped at 1900 bytes on save (roughly 40 short lines).

#ifndef MEMO_CFG_H
#define MEMO_CFG_H

#define MEMO_KEY "memos"
#define MEMO_MAX_LEN 1900

String memoGet() {
  return NVS.getString(MEMO_KEY);
}

#endif
