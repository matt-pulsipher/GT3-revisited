#pragma once

// The original's car record (0x16DC bytes; car = conductor + 0xC8 + 0x16DC i) in the interpreter as sim::exact::Car:
// reading and writing records through the field table (src/sim/exact/car_fields.h), and the record words a call changed
// that no field covers (so a port knows which fields to add). Only addresses and layouts; the code that runs is the user's
// disc.

#include "ee.h"
#include "program_map.h"

#include "sim/exact/car_fields.h"
#include "sim/exact/program_data.h"

#include <string>
#include <type_traits>
#include <vector>

namespace gt3r {
class Disc;
}

namespace gt3r::diff::car_exact {

namespace ex = sim::exact;

using ex::kCarSize;
using ex::kF;
using ex::kWheel0;
using ex::kWheelSize;
using ex::kGround0;
using ex::kGroundSize;
using ex::kParams;
using ex::kDrive0;
using ex::kDriveSize;
using ex::fields;
using ex::first_difference;
using ex::copy_changed;

ex::Car read_car(Ee& ee, u32 car);
void write_car(Ee& ee, u32 car, const ex::Car& c);
// The words a ground query that misses leaves from the program's own stack (the single-point query 0x18FA80 at the race start:
// each missed wheel record's normal, plane and raw scalars, CourseQuery::ground) taken from the original's record into ours,
// for a start off the road (a grid slot past the course's table). The count of wheel records taken.
int take_missed_ground(ex::Car& ours, const ex::Car& theirs);
// Raw record bytes in NTSC-U's layout (0x16DC bytes: a later build's record without its extra word, program_map.h's
// car_extra_word(), and with the feedback words its conductor keeps for the car at NTSC-U's places in the record: the
// bytes +0x16D4 .. +0x16D6 and the force filter F +0x514), written back by write_image() (the extra word untouched), and the offsets (word-aligned) where two snapshots differ outside every field of the table.
std::vector<u8> snapshot(Ee& ee, u32 car);
void write_image(Ee& ee, u32 car, const u8* image);
std::vector<u32> unmirrored_changes(const std::vector<u8>& pre, const std::vector<u8>& post);
// The program's data tables (sim/exact/program_data.h) from the disc's CORE.GT3, read once.
const ex::ProgramData& program_data(Disc& disc);

} // namespace gt3r::diff::car_exact
