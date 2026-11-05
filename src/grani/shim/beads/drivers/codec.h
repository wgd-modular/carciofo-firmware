// Copyright 2018 Fred's Lab and Mutable Instruments.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
// Author: Frédéric Meslin (fred@fredslab.net)
// Author: Oliver Rockstedt (info@sourcebox.de)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Only the frame type of Beads' PCM3060 driver, which the gain control reads.
// The Daisy's own audio driver replaces the rest.

#ifndef BEADS_DRIVERS_CODEC_H_
#define BEADS_DRIVERS_CODEC_H_

#include "stmlib/stmlib.h"

namespace beads {

const size_t kMaxCodecBlockSize = 24;

class Codec {
 public:
  struct Frame {
    int32_t l;
    int32_t r;
  } __attribute__((packed, aligned(4)));
};

}  // namespace beads

#endif  // BEADS_DRIVERS_CODEC_H_
