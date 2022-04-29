#include <nonstd/expected.hpp>
#include <nrg/nrg.hpp>

#include <thread>
#include <utility>

namespace {
template <typename Location>
std::pair<nrgprf::joules<double>, nrgprf::joules<double>>
get_readings(const nrgprf::reader_rapl &reader, const nrgprf::sample &first,
             const nrgprf::sample &last, uint8_t socket) {
  using nrgprf::exception;
  auto energy_first = reader.value<Location>(first, socket);
  if (!energy_first)
    throw exception(energy_first.error());
  auto energy_last = reader.value<Location>(last, socket);
  if (!energy_last)
    throw exception(energy_last.error());
  return {*energy_first, *energy_last};
}
} // namespace

int main() {
  try {
    using namespace nrgprf;
    constexpr uint8_t socket = 0;

    reader_rapl reader(locmask::pkg, 0x1);
    sample first;
    sample last;
    if (std::error_code ec; !reader.read(first, ec))
      throw exception(ec);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    if (std::error_code ec; !reader.read(last, ec))
      throw exception(ec);
    auto [before, after] = get_readings<loc::pkg>(reader, first, last, socket);

    std::cout << "Before sleep: " << before.count() << " J\n";
    std::cout << "After sleep: " << after.count() << " J\n";
    std::cout << "Energy consumed: " << (after - before).count() << " J\n";
  } catch (const nrgprf::exception &e) {
    std::cerr << "NRG exception: " << e.what() << '\n';
    return 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
