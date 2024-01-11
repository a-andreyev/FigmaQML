#pragma once

#include <qul/singleton.h>
#include <qul/signal.h>

#define BUG_199 // There is some issue (bug?) in Qt for MCU and cmake definitions are not always working!

/// https://bugreports.qt.io/browse/QTMCU-199, until fixed
#ifdef BUG_199
#include "qul/private/unicodestring.h"
using SignalString = Qul::Private::String;
#else
#include <string>
using SignalString = std::string;
#endif

class FigmaQmlSingleton : public Qul::Singleton<FigmaQmlSingleton> {
public:
    Qul::Signal<void(SignalString element, SignalString value)> setValue;
    void requestValue(SignalString element, SignalString value) {setValue(element, value);}
};

