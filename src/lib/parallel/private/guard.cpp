#include "guard.hpp"

#include "mutex.hpp"

namespace sky::parallel {

guard::guard(mutex& target) : mutex_(target) {
}

} // namespace sky::parallel
