#ifndef _MAPUTIL_H
#define _MAPUTIL_H

// Private helpers shared by the mappers. Not installed and not public API.

// Clamps a pedal value to 0.0..1.0. NaN and negative values (e.g. a sim's
// -1 "not available") become 0.0.
static inline double clamp_pedal(double d)
{
    if (!(d > 0.0))
    {
        return 0.0;
    }
    if (d > 1.0)
    {
        return 1.0;
    }
    return d;
}

#endif
