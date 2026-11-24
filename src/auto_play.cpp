// SPDX-License-Identifier: GPL-2.0-or-later
#include "auto_play.hpp"
#include "state_io.hpp"
#include <algorithm>
namespace whj2 {
void AutoPlay::tick(double ms,size_t visible,double textWait,double textSpeed){if(visible>=counted)remaining+=(visible-counted)*std::max(textWait,textSpeed);counted=visible;remaining=std::max(0.,remaining-std::max(0.,ms));}
qlie::Bytes AutoPlay::save()const{StateWriter w;w.str("WHJ2-auto-1");w.n(enabled);w.n(counted);w.f(float(remaining));return w.bytes;}
void AutoPlay::restore(const qlie::Bytes&b){StateReader r{b};if(r.str()!="WHJ2-auto-1")throw std::runtime_error("Auto state version");enabled=r.n()!=0;counted=size_t(r.n());remaining=r.f();r.end();}
}
