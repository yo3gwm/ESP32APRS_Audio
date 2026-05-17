#pragma once

// Build timestamp generated at compile time from compiler macros.
// __DATE__ = "Mmm DD YYYY"  e.g. "May 14 2026"
// __TIME__ = "HH:MM:SS"    e.g. "09:03:45"
// Result format: YYYYMMDD_HHMM e.g. "20260514_0903"

namespace build_info
{
    static constexpr const char _d[] = __DATE__;
    static constexpr const char _t[] = __TIME__;

    static constexpr int _month()
    {
        return _d[0]=='J' && _d[1]=='a' ?  1 :  // Jan
               _d[0]=='F'               ?  2 :  // Feb
               _d[0]=='M' && _d[2]=='r' ?  3 :  // Mar
               _d[0]=='A' && _d[1]=='p' ?  4 :  // Apr
               _d[0]=='M'               ?  5 :  // May
               _d[0]=='J' && _d[2]=='n' ?  6 :  // Jun
               _d[0]=='J'               ?  7 :  // Jul
               _d[0]=='A'               ?  8 :  // Aug
               _d[0]=='S'               ?  9 :  // Sep
               _d[0]=='O'               ? 10 :  // Oct
               _d[0]=='N'               ? 11 :  // Nov
                                          12;   // Dec
    }

    static constexpr char timestamp[14] = {
        _d[7], _d[8], _d[9], _d[10],               // YYYY
        (char)('0' + _month() / 10),                // MM tens
        (char)('0' + _month() % 10),                // MM units
        _d[4] == ' ' ? '0' : _d[4],                // DD tens (space-pad for day 1-9)
        _d[5],                                       // DD units
        '_',
        _t[0], _t[1],                               // HH
        _t[3], _t[4],                               // MM
        '\0'
    };
}

#define BUILD_TIMESTAMP (build_info::timestamp)
