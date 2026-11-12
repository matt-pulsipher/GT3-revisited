#pragma once

// Which seat a car's driver sits in, read from its body (CarVisual::driver_seat). GT3 keeps no
// steering side in its tables, and every car's camera points are on its centre line, but most
// car bodies hold their driver: a small figure in one seat (often 16 or 20 triangles with a
// picture of its own, 0.2 to 0.4 m off the centre line), and nothing like it in the other.
//
// The body's LOD-0 mesh is mirrored in its centre plane, and the triangles with no mirror
// image are added up by area in each seat's lane: between the middle and the doors, in the
// cabin's part of the length, above the waist. Bodywork is symmetric, so what is left there is
// the driver (with the seat and wheel some cars model round it). A lane with clearly more than
// the other has the driver. Single-seaters, the cars with a figure in each seat (several rally
// cars and their co-drivers) and the bodies with no driver give no side.
//
// It needs no per-car table and reads whatever body is drawn: the disc's cars on every release
// (their models are the same: the MX-5 the US release calls a Miata has its driver on the
// right) and fitted replacement bodies.

#include "scene/model.h"

namespace gt3r::race {

struct DriverSeat {
    int side = 0;              // -1 the left seat, 1 the right, 0: the body doesn't say
    float x = 0;               // the driver's place across the car (model frame, x right; metres)
    float left = 0, right = 0; // the unmirrored area in each seat's lane (m2)
};

DriverSeat find_driver_seat(const scene::Model& body);

} // namespace gt3r::race
