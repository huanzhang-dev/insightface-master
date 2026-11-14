// record.h - one registered person: what the database holds for him.
#ifndef FA_RECORD_H
#define FA_RECORD_H

#include <string>

#include "types.h"

namespace fa {

struct Record {
    std::string id;
    std::string name;
    Feature feature;
    std::string card_id;  // RF card number of the person; empty = no card (unique among the records)
};

}  // namespace fa

#endif  // FA_RECORD_H
