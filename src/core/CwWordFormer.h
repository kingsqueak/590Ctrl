#pragma once

#include <QString>

namespace ts590 {

// Puts back word spaces a CW decoder missed: "CQSSK1XMK1XM" becomes
// "CQ SS K1XM K1XM", "73AE0QDE" becomes "73 AE0Q DE". A run of characters is
// split only when every piece is something known: a word from cw-words.txt,
// a callsign, a report, a number or a grid square. Anything else is left as
// the decoder printed it, so garbled copy isn't chopped into nonsense.
// Callsigns already seen standing alone in the text are preferred, which
// settles most ambiguous runs.
//
// It works on the whole text each time, so earlier words can tidy up once a
// callsign has been seen. The last word is left alone until a space follows.
QString formCwWords(const QString &text);

}   // namespace ts590
