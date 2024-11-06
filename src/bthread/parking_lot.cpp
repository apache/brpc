#include "parking_lot.h"

namespace bthread {

butil::atomic<int> ParkingLot::_waiting_count{0};

} // namespace bthread