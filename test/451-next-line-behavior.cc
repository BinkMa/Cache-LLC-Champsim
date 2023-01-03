#include "catch.hpp"
#include "mocks.hpp"
#include "cache.h"
#include "champsim_constants.h"

SCENARIO("The next line prefetcher issues prefetches") {
  GIVEN("An empty cache") {
    do_nothing_MRC mock_ll;
    to_rq_MRP mock_ul;
    CACHE uut{"451-uut", 1, 1, 8, 32, 1, 3, 1, 1, 0, false, false, false, (1<<LOAD)|(1<<PREFETCH), {&mock_ul.queues}, nullptr, &mock_ll.queues, CACHE::pprefetcherDnext_line, CACHE::rreplacementDlru};

    std::array<champsim::operable*, 3> elements{{&mock_ll, &mock_ul, &uut}};

    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }

    WHEN("A packet is issued") {
      // Create a test packet
      static uint64_t id = 1;
      decltype(mock_ul)::request_type seed;
      seed.address = 0xffff'003f;
      seed.instr_id = id++;
      seed.cpu = 0;
      seed.to_return = {&mock_ul.returned};

      // Issue it to the uut
      auto seed_result = mock_ul.issue(seed);
      THEN("The issue is accepted") {
        REQUIRE(seed_result);
      }

      // Run the uut for a bunch of cycles to clear it out of the RQ and fill the cache
      for (auto i = 0; i < 100; ++i)
        for (auto elem : elements)
          elem->_operate();

      THEN("A total of 2 requests were generated") {
        REQUIRE(mock_ll.packet_count() == 2);
      }

      THEN("All of the issued requests have the same stride") {
        REQUIRE((mock_ll.addresses.at(0) >> LOG2_BLOCK_SIZE) + 1 == (mock_ll.addresses.at(1) >> LOG2_BLOCK_SIZE));
      }
    }
  }
}



