// Code submitted for the Third Data Prefetching Championship
//
// Author: Alberto Ros, University of Murcia
//
// Paper #13: Berti: A Per-Page Best-Request-Time Delta Prefetcher

#include "cache.h"
#include "channel.h"
#include <cassert>
#include <iostream>
using namespace std;

#define LLC_PAGE_BLOCKS_BITS (LOG2_PAGE_SIZE - LOG2_BLOCK_SIZE)
#define LLC_PAGE_BLOCKS (1 << LLC_PAGE_BLOCKS_BITS)
#define LLC_PAGE_OFFSET_MASK (LLC_PAGE_BLOCKS - 1)

#define LLC_MAX_NUM_BURST_PREFETCHES 3

#define LLC_BERTI_CTR_MED_HIGH_CONFIDENCE 2

// TIME AND OVERFLOWS

#define LLC_TIME_BITS 16
#define LLC_TIME_OVERFLOW ((uint64_t)1 << LLC_TIME_BITS)
#define LLC_TIME_MASK (LLC_TIME_OVERFLOW - 1)

uint64_t llc_get_latency(uint64_t cycle, uint64_t cycle_prev) {
  return cycle - cycle_prev;
  uint64_t cycle_masked = cycle & LLC_TIME_MASK;
  uint64_t cycle_prev_masked = cycle_prev & LLC_TIME_MASK;
  if (cycle_prev_masked > cycle_masked) {
    return (cycle_masked + LLC_TIME_OVERFLOW) - cycle_prev_masked;
  }
  return cycle_masked - cycle_prev_masked;
}

// STRIDE

int llc_calculate_stride(uint64_t prev_offset, uint64_t current_offset) {
  int stride;
  if (current_offset > prev_offset) {
    stride = current_offset - prev_offset;
  } else {
    stride = prev_offset - current_offset;
    stride *= -1;
  }
  return stride;
}


// CURRENT PAGES TABLE

#define LLC_CURRENT_PAGES_TABLE_INDEX_BITS 6
#define LLC_CURRENT_PAGES_TABLE_ENTRIES (((1 << LLC_CURRENT_PAGES_TABLE_INDEX_BITS) * NUM_CPUS) - 1) // Null pointer for prev_request
#define LLC_CURRENT_PAGES_TABLE_NUM_BERTI 10
#define LLC_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS 7

typedef struct __llc_current_page_entry {
  uint64_t page_addr; // 52 bits
  uint64_t ip; // 10 bits
  uint64_t u_vector; // 64 bits
  uint64_t first_offset; // 6 bits
  int berti[LLC_CURRENT_PAGES_TABLE_NUM_BERTI]; // 70 bits
  unsigned berti_ctr[LLC_CURRENT_PAGES_TABLE_NUM_BERTI]; // 60 bits
  uint64_t last_burst; // 6 bits
  uint64_t lru; // 6 bits
} llc_current_page_entry;

llc_current_page_entry llc_current_pages_table[LLC_CURRENT_PAGES_TABLE_ENTRIES];

void llc_init_current_pages_table() {
  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_ENTRIES; i++) {
    llc_current_pages_table[i].page_addr = 0;
    llc_current_pages_table[i].ip = 0;
    llc_current_pages_table[i].u_vector = 0; // not valid
    llc_current_pages_table[i].last_burst = 0;
    llc_current_pages_table[i].lru = i;
  }
}

uint64_t llc_get_current_pages_entry(uint64_t page_addr) {
  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_ENTRIES; i++) {
    if (llc_current_pages_table[i].page_addr == page_addr) return i;
  }
  return LLC_CURRENT_PAGES_TABLE_ENTRIES;
}

void llc_update_lru_current_pages_table(uint64_t index) {
  assert(index < LLC_CURRENT_PAGES_TABLE_ENTRIES);
  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_ENTRIES; i++) {
    if (llc_current_pages_table[i].lru < llc_current_pages_table[index].lru) { // Found
      llc_current_pages_table[i].lru++;
    }
  }
  llc_current_pages_table[index].lru = 0;
}

uint64_t llc_get_lru_current_pages_entry() {
  uint64_t lru = LLC_CURRENT_PAGES_TABLE_ENTRIES;
  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_ENTRIES; i++) {
    llc_current_pages_table[i].lru++;
    if (llc_current_pages_table[i].lru == LLC_CURRENT_PAGES_TABLE_ENTRIES) {
      llc_current_pages_table[i].lru = 0;
      lru = i;
    } 
  }
  assert(lru != LLC_CURRENT_PAGES_TABLE_ENTRIES);
  return lru;
}

int llc_get_berti_current_pages_table(uint64_t index, uint64_t &ctr) {
  assert(index < LLC_CURRENT_PAGES_TABLE_ENTRIES);
  uint64_t max_score = 0;
  uint64_t berti = 0;
  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_NUM_BERTI; i++) {
    uint64_t score; 
    score = llc_current_pages_table[index].berti_ctr[i];
    if (score > max_score) {
      berti = llc_current_pages_table[index].berti[i];
      max_score = score;
      ctr = llc_current_pages_table[index].berti_ctr[i];
    }
  }
  return berti;
}

void llc_add_current_pages_table(uint64_t index, uint64_t page_addr, uint64_t ip, uint64_t offset) {
  assert(index < LLC_CURRENT_PAGES_TABLE_ENTRIES);
  llc_current_pages_table[index].page_addr = page_addr;
  llc_current_pages_table[index].ip = ip;
  llc_current_pages_table[index].u_vector = (uint64_t)1 << offset;
  llc_current_pages_table[index].first_offset = offset;
  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_NUM_BERTI; i++) {
    llc_current_pages_table[index].berti_ctr[i] = 0;
  }
  llc_current_pages_table[index].last_burst = 0;
}

uint64_t llc_update_demand_current_pages_table(uint64_t index, uint64_t offset) {
  assert(index < LLC_CURRENT_PAGES_TABLE_ENTRIES);
  llc_current_pages_table[index].u_vector |= (uint64_t)1 << offset;
  llc_update_lru_current_pages_table(index);
  return llc_current_pages_table[index].ip;
}

void llc_add_berti_current_pages_table(uint64_t index, int berti) {
  assert(berti != 0);
  assert(index < LLC_CURRENT_PAGES_TABLE_ENTRIES);
  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_NUM_BERTI; i++) {
    if (llc_current_pages_table[index].berti_ctr[i] == 0) {
      llc_current_pages_table[index].berti[i] = berti;
      llc_current_pages_table[index].berti_ctr[i] = 1;
      break;
    } else if (llc_current_pages_table[index].berti[i] == berti) {
      llc_current_pages_table[index].berti_ctr[i]++;
      break;
    }
  }
  llc_update_lru_current_pages_table(index);
}

bool llc_requested_offset_current_pages_table(uint64_t index, uint64_t offset) {
  assert(index < LLC_CURRENT_PAGES_TABLE_ENTRIES);
  return llc_current_pages_table[index].u_vector & ((uint64_t)1 << offset);
}

void llc_remove_current_table_entry(uint64_t index) {
  llc_current_pages_table[index].page_addr = 0;
  llc_current_pages_table[index].u_vector = 0;
  llc_current_pages_table[index].berti[0] = 0;
}


// PREVIOUS REQUESTS TABLE

#define LLC_PREV_REQUESTS_TABLE_INDEX_BITS 10
#define LLC_PREV_REQUESTS_TABLE_ENTRIES ((1 << LLC_PREV_REQUESTS_TABLE_INDEX_BITS) * NUM_CPUS)
#define LLC_PREV_REQUESTS_TABLE_MASK (LLC_PREV_REQUESTS_TABLE_ENTRIES - 1)
#define LLC_PREV_REQUESTS_TABLE_NULL_POINTER LLC_CURRENT_PAGES_TABLE_ENTRIES

typedef struct __llc_prev_request_entry {
  uint64_t page_addr_pointer; // 6 bits
  uint64_t offset; // 6 bits
  uint64_t time; // 16 bits
} llc_prev_request_entry;

llc_prev_request_entry llc_prev_requests_table[LLC_PREV_REQUESTS_TABLE_ENTRIES];
uint64_t llc_prev_requests_table_head;

void llc_init_prev_requests_table() {
  llc_prev_requests_table_head = 0;
  for (int i = 0; i < LLC_PREV_REQUESTS_TABLE_ENTRIES; i++) {
    llc_prev_requests_table[i].page_addr_pointer = LLC_PREV_REQUESTS_TABLE_NULL_POINTER;
  }
}

uint64_t llc_find_prev_request_entry(uint64_t pointer, uint64_t offset) {
  for (int i = 0; i < LLC_PREV_REQUESTS_TABLE_ENTRIES; i++) {
    if (llc_prev_requests_table[i].page_addr_pointer == pointer
	&& llc_prev_requests_table[i].offset == offset) return i;
  }
  return LLC_PREV_REQUESTS_TABLE_ENTRIES;
}

void llc_add_prev_requests_table(uint64_t pointer, uint64_t offset, uint64_t cycle) {
  // First find for coalescing
  if (llc_find_prev_request_entry(pointer, offset) != LLC_PREV_REQUESTS_TABLE_ENTRIES) return;

  // Allocate a new entry (evict old one if necessary)
  llc_prev_requests_table[llc_prev_requests_table_head].page_addr_pointer = pointer;
  llc_prev_requests_table[llc_prev_requests_table_head].offset = offset;
  llc_prev_requests_table[llc_prev_requests_table_head].time = cycle & LLC_TIME_MASK;
  llc_prev_requests_table_head = (llc_prev_requests_table_head + 1) & LLC_PREV_REQUESTS_TABLE_MASK;
}

void llc_reset_pointer_prev_requests(uint64_t pointer) {
  for (int i = 0; i < LLC_PREV_REQUESTS_TABLE_ENTRIES; i++) {
    if (llc_prev_requests_table[i].page_addr_pointer == pointer) {
      llc_prev_requests_table[i].page_addr_pointer = LLC_PREV_REQUESTS_TABLE_NULL_POINTER;
    }
  }
}

uint64_t llc_get_latency_prev_requests_table(uint64_t pointer, uint64_t offset, uint64_t cycle) {
  uint64_t index = llc_find_prev_request_entry(pointer, offset); 
  if (index == LLC_PREV_REQUESTS_TABLE_ENTRIES) return 0;
  return llc_get_latency(cycle, llc_prev_requests_table[index].time);
}

void llc_get_berti_prev_requests_table(uint64_t pointer, uint64_t offset, uint64_t cycle, int *berti) {
  int my_pos = 0;
  uint64_t extra_time = 0;
  uint64_t last_time = llc_prev_requests_table[(llc_prev_requests_table_head + LLC_PREV_REQUESTS_TABLE_MASK) & LLC_PREV_REQUESTS_TABLE_MASK].time;
  for (uint64_t i = (llc_prev_requests_table_head + LLC_PREV_REQUESTS_TABLE_MASK) & LLC_PREV_REQUESTS_TABLE_MASK; i != llc_prev_requests_table_head; i = (i + LLC_PREV_REQUESTS_TABLE_MASK) & LLC_PREV_REQUESTS_TABLE_MASK) {
    // Against the time overflow
    if (last_time < llc_prev_requests_table[i].time) {
      extra_time = LLC_TIME_OVERFLOW;
    }
    last_time = llc_prev_requests_table[i].time;  
    if (llc_prev_requests_table[i].page_addr_pointer == pointer) {
      if (llc_prev_requests_table[i].time <= (cycle & LLC_TIME_MASK) + extra_time) {
	berti[my_pos] = llc_calculate_stride(llc_prev_requests_table[i].offset, offset);
	my_pos++;
	if (my_pos == LLC_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS) return;
      }
    }
  }
  berti[my_pos] = 0;
}


// PREVIOUS PREFETCHES TABLE

#define LLC_PREV_PREFETCHES_TABLE_INDEX_BITS 9
#define LLC_PREV_PREFETCHES_TABLE_ENTRIES ((1 << LLC_PREV_PREFETCHES_TABLE_INDEX_BITS) * NUM_CPUS)
#define LLC_PREV_PREFETCHES_TABLE_MASK (LLC_PREV_PREFETCHES_TABLE_ENTRIES - 1)
#define LLC_PREV_PREFETCHES_TABLE_NULL_POINTER LLC_CURRENT_PAGES_TABLE_ENTRIES

// We do not have access to the MSHR, so we aproximate it using this structure.
typedef struct __llc_prev_prefetch_entry {
  uint64_t page_addr_pointer; // 6 bits
  uint64_t offset; // 6 bits
  uint64_t time_lat; // 16 bits // time if not completed, latency if completed
  bool completed;
} llc_prev_prefetch_entry;

llc_prev_prefetch_entry llc_prev_prefetches_table[LLC_PREV_PREFETCHES_TABLE_ENTRIES];
uint64_t llc_prev_prefetches_table_head;

void llc_init_prev_prefetches_table() {
  llc_prev_prefetches_table_head = 0;
  for (int i = 0; i < LLC_PREV_PREFETCHES_TABLE_ENTRIES; i++) {
    llc_prev_prefetches_table[i].page_addr_pointer = LLC_PREV_PREFETCHES_TABLE_NULL_POINTER;
  }
}

uint64_t llc_find_prev_prefetch_entry(uint64_t pointer, uint64_t offset) {
  for (int i = 0; i < LLC_PREV_PREFETCHES_TABLE_ENTRIES; i++) {
    if (llc_prev_prefetches_table[i].page_addr_pointer == pointer
	&& llc_prev_prefetches_table[i].offset == offset) return i;
  }
  return LLC_PREV_PREFETCHES_TABLE_ENTRIES;
}

void llc_add_prev_prefetches_table(uint64_t pointer, uint64_t offset, uint64_t cycle) {
  // First find for coalescing
  if (llc_find_prev_prefetch_entry(pointer, offset) != LLC_PREV_PREFETCHES_TABLE_ENTRIES) return;

  // Allocate a new entry (evict old one if necessary)
  llc_prev_prefetches_table[llc_prev_prefetches_table_head].page_addr_pointer = pointer;
  llc_prev_prefetches_table[llc_prev_prefetches_table_head].offset = offset;
  llc_prev_prefetches_table[llc_prev_prefetches_table_head].time_lat = cycle & LLC_TIME_MASK;
  llc_prev_prefetches_table[llc_prev_prefetches_table_head].completed = false;
  llc_prev_prefetches_table_head = (llc_prev_prefetches_table_head + 1) & LLC_PREV_PREFETCHES_TABLE_MASK;
}

void llc_reset_pointer_prev_prefetches(uint64_t pointer) {
  for (int i = 0; i < LLC_PREV_PREFETCHES_TABLE_ENTRIES; i++) {
    if (llc_prev_prefetches_table[i].page_addr_pointer == pointer) {
      llc_prev_prefetches_table[i].page_addr_pointer = LLC_PREV_PREFETCHES_TABLE_NULL_POINTER;
    }
  }
}

void llc_reset_entry_prev_prefetches_table(uint64_t pointer, uint64_t offset) {
  uint64_t index = llc_find_prev_prefetch_entry(pointer, offset);
  if (index != LLC_PREV_PREFETCHES_TABLE_ENTRIES) {
    llc_prev_prefetches_table[index].page_addr_pointer = LLC_PREV_PREFETCHES_TABLE_NULL_POINTER;
  }
}

uint64_t llc_get_and_set_latency_prev_prefetches_table(uint64_t pointer, uint64_t offset, uint64_t cycle) {
  uint64_t index = llc_find_prev_prefetch_entry(pointer, offset); 
  if (index == LLC_PREV_PREFETCHES_TABLE_ENTRIES) return 0;
  if (!llc_prev_prefetches_table[index].completed) {
    llc_prev_prefetches_table[index].time_lat = llc_get_latency(cycle, llc_prev_prefetches_table[index].time_lat);
    llc_prev_prefetches_table[index].completed = true;
  }    
  return llc_prev_prefetches_table[index].time_lat;
}

uint64_t llc_get_latency_prev_prefetches_table(uint64_t pointer, uint64_t offset) {
  uint64_t index = llc_find_prev_prefetch_entry(pointer, offset);
  if (index == LLC_PREV_PREFETCHES_TABLE_ENTRIES) return 0;
  if (!llc_prev_prefetches_table[index].completed) return 0;
  return llc_prev_prefetches_table[index].time_lat;
}


// RECORD PAGES TABLE

//#define LLC_RECORD_PAGES_TABLE_INDEX_BITS 8
#define LLC_RECORD_PAGES_TABLE_ENTRIES ((((1 << 8) + (1 << 7)) * NUM_CPUS) - 1) // Null pointer for ip table
#define LLC_TRUNCATED_PAGE_ADDR_BITS 32 // 4 bytes
#define LLC_TRUNCATED_PAGE_ADDR_MASK (((uint64_t)1 << LLC_TRUNCATED_PAGE_ADDR_BITS) -1)

typedef struct __llc_record_page_entry {
  uint64_t page_addr; // 4 bytes
  uint64_t u_vector; // 8 bytes
  uint64_t first_offset; // 6 bits
  int berti; // 7 bits
  uint64_t lru; // 9 bits
} llc_record_page_entry;

llc_record_page_entry llc_record_pages_table[LLC_RECORD_PAGES_TABLE_ENTRIES];

void llc_init_record_pages_table() {
  for (int i = 0; i < LLC_RECORD_PAGES_TABLE_ENTRIES; i++) {
    llc_record_pages_table[i].page_addr = 0;
    llc_record_pages_table[i].u_vector = 0;
    llc_record_pages_table[i].lru = i;
  }
}

uint64_t llc_get_lru_record_pages_entry() {
  uint64_t lru = LLC_RECORD_PAGES_TABLE_ENTRIES;
  for (int i = 0; i < LLC_RECORD_PAGES_TABLE_ENTRIES; i++) {
    llc_record_pages_table[i].lru++;
    if (llc_record_pages_table[i].lru == LLC_RECORD_PAGES_TABLE_ENTRIES) {
      llc_record_pages_table[i].lru = 0;
      lru = i;
    } 
  }
  assert(lru != LLC_RECORD_PAGES_TABLE_ENTRIES);
  return lru;
}

void llc_update_lru_record_pages_table(uint64_t index) {
  // assert(index < LLC_RECORD_PAGES_TABLE_ENTRIES);
  for (int i = 0; i < LLC_RECORD_PAGES_TABLE_ENTRIES; i++) {
    if (llc_record_pages_table[i].lru < llc_record_pages_table[index].lru) { // Found
      llc_record_pages_table[i].lru++;
    }
  }
  llc_record_pages_table[index].lru = 0;
}

void llc_add_record_pages_table(uint64_t index, uint64_t page_addr, uint64_t vector, uint64_t first_offset, int berti) {
  // assert(index < LLC_RECORD_PAGES_TABLE_ENTRIES);
  llc_record_pages_table[index].page_addr = page_addr & LLC_TRUNCATED_PAGE_ADDR_MASK;
  llc_record_pages_table[index].u_vector = vector;
  llc_record_pages_table[index].first_offset = first_offset;
  llc_record_pages_table[index].berti = berti;    
  llc_update_lru_record_pages_table(index);
}

uint64_t llc_get_entry_record_pages_table(uint64_t page_addr, uint64_t first_offset) {
  uint64_t trunc_page_addr = page_addr & LLC_TRUNCATED_PAGE_ADDR_MASK;
  for (int i = 0; i < LLC_RECORD_PAGES_TABLE_ENTRIES; i++) {
    if (llc_record_pages_table[i].page_addr == trunc_page_addr
	&& llc_record_pages_table[i].first_offset == first_offset) { // Found
      return i;
    }
  }
  return LLC_RECORD_PAGES_TABLE_ENTRIES;
}

uint64_t llc_get_entry_record_pages_table(uint64_t page_addr) {
  uint64_t trunc_page_addr = page_addr & LLC_TRUNCATED_PAGE_ADDR_MASK;  
  for (int i = 0; i < LLC_RECORD_PAGES_TABLE_ENTRIES; i++) {
    if (llc_record_pages_table[i].page_addr == trunc_page_addr) { // Found
      return i;
    }
  }
  return LLC_RECORD_PAGES_TABLE_ENTRIES;
}

void llc_copy_entries_record_pages_table(uint64_t index_from, uint64_t index_to) {
  assert(index_from < LLC_RECORD_PAGES_TABLE_ENTRIES);
  assert(index_to < LLC_RECORD_PAGES_TABLE_ENTRIES);
  llc_record_pages_table[index_to].page_addr = llc_record_pages_table[index_from].page_addr;
  llc_record_pages_table[index_to].u_vector = llc_record_pages_table[index_from].u_vector;
  llc_record_pages_table[index_to].first_offset = llc_record_pages_table[index_from].first_offset;
  llc_record_pages_table[index_to].berti = llc_record_pages_table[index_from].berti;    
  llc_update_lru_record_pages_table(index_to);
}


// IP TABLE

#define LLC_IP_TABLE_INDEX_BITS 10
#define LLC_IP_TABLE_ENTRIES ((1 << LLC_IP_TABLE_INDEX_BITS) * NUM_CPUS)
#define LLC_IP_TABLE_INDEX_MASK (LLC_IP_TABLE_ENTRIES - 1)
#define LLC_IP_TABLE_NULL_POINTER LLC_RECORD_PAGES_TABLE_ENTRIES

uint64_t llc_ip_table[LLC_IP_TABLE_ENTRIES]; // 9 bits

void llc_init_ip_table() {
  for (int i = 0; i < LLC_IP_TABLE_ENTRIES; i++) {
    llc_ip_table[i] = LLC_IP_TABLE_NULL_POINTER;
  }
}


// TABLE MOVEMENTS

// Sumarizes the content to the current page to be evicted
// From all timely requests found, we record the best 
void llc_record_current_page(uint64_t index_current) {
  if (llc_current_pages_table[index_current].u_vector) { // Valid entry
    uint64_t record_index = llc_ip_table[llc_current_pages_table[index_current].ip & LLC_IP_TABLE_INDEX_MASK];
    // assert(record_index < LLC_RECORD_PAGES_TABLE_ENTRIES);
    uint64_t confidence;
    llc_add_record_pages_table(record_index,
			       llc_current_pages_table[index_current].page_addr,
			       llc_current_pages_table[index_current].u_vector,
			       llc_current_pages_table[index_current].first_offset,
			       llc_get_berti_current_pages_table(index_current, confidence));
  }
}

// INTERFACE
void CACHE::prefetcher_cycle_operate() {}

void CACHE::prefetcher_initialize() 
{
  cout << "CPU " << cpu << " LLC Berti prefetcher" << endl;
  
  llc_init_current_pages_table();
  llc_init_prev_requests_table();
  llc_init_prev_prefetches_table();
  llc_init_record_pages_table();
  llc_init_ip_table();
}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit,bool useful_prefetch, uint8_t type, uint32_t metadata_in)
{
  uint64_t line_addr = addr >> LOG2_BLOCK_SIZE;
  uint64_t page_addr = line_addr >> LLC_PAGE_BLOCKS_BITS;
  uint64_t offset = line_addr & LLC_PAGE_OFFSET_MASK;

  // Update current pages table
  // Find the entry
  uint64_t index = llc_get_current_pages_entry(page_addr);

  // If not accessed recently
  if (index == LLC_CURRENT_PAGES_TABLE_ENTRIES
      || !llc_requested_offset_current_pages_table(index, offset)) {
        
    if (index < LLC_CURRENT_PAGES_TABLE_ENTRIES) { // Found
    
      // If offset found, already requested, so return;
      if (llc_requested_offset_current_pages_table(index, offset)) return metadata_in;

      uint64_t first_ip = llc_update_demand_current_pages_table(index, offset);
      // assert(llc_ip_table[first_ip & LLC_IP_TABLE_INDEX_MASK] != LLC_IP_TABLE_NULL_POINTER);

      // Update berti
      if (cache_hit) {
	uint64_t pref_latency = llc_get_latency_prev_prefetches_table(index, offset);
	if (pref_latency != 0) {
	  // Find berti distance from pref_latency cycles before
	  int berti[LLC_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS]; 
	  llc_get_berti_prev_requests_table(index, offset, current_core_cycle[cpu] - pref_latency, berti);
	  for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS; i++) {
	    if (berti[i] == 0) break; 
	    assert(abs(berti[i]) < LLC_PAGE_BLOCKS);
	    llc_add_berti_current_pages_table(index, berti[i]);
	  }

	  // Eliminate a prev prefetch since it has been usedis
	  llc_reset_entry_prev_prefetches_table(index, offset);
      	}
      }
      
      if (first_ip != ip) {
	// Assign same pointer to group IPs
	llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK] = llc_ip_table[first_ip & LLC_IP_TABLE_INDEX_MASK]; 
      }
    } else { // Not found: Add entry
     
      // Find victim and clear pointers to it
      uint64_t victim_index = llc_get_lru_current_pages_entry(); // already updates lru
      assert(victim_index < LLC_CURRENT_PAGES_TABLE_ENTRIES);
      llc_reset_pointer_prev_requests(victim_index); // Not valid anymore
      llc_reset_pointer_prev_prefetches(victim_index); // Not valid anymore

      // Copy victim to record table
      llc_record_current_page(victim_index);

      // Add new current page
      index = victim_index;
      llc_add_current_pages_table(index, page_addr, ip & LLC_IP_TABLE_INDEX_MASK, offset);

      // Set pointer in IP table
      uint64_t index_record = llc_get_entry_record_pages_table(page_addr, offset);
      // The ip pointer is null
      if (llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK] == LLC_IP_TABLE_NULL_POINTER) { 
	if (index_record == LLC_RECORD_PAGES_TABLE_ENTRIES) { // Page not recorded
	  // Get free record page pointer.
	  uint64_t new_pointer = llc_get_lru_record_pages_entry();
	  llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK] = new_pointer;
	} else {
	  llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK] = index_record;
	}
      } else if (llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK] != index_record) {
	// If the current IP is valid, but points to another address
	// we replicate it in another record entry (lru)
	// such that the recorded page is not deleted when the current entry summarizes
	uint64_t new_pointer = llc_get_lru_record_pages_entry();
	llc_copy_entries_record_pages_table(llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK], new_pointer);
	llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK] = new_pointer;
      }
    }

    llc_add_prev_requests_table(index, offset, current_core_cycle[cpu]);
       
    // PREDICT
    uint64_t u_vector = 0;
    uint64_t first_offset = llc_current_pages_table[index].first_offset;
    int berti = 0;
    bool recorded = false;
    
    uint64_t ip_pointer = llc_ip_table[ip & LLC_IP_TABLE_INDEX_MASK];
    uint64_t pgo_pointer = llc_get_entry_record_pages_table(page_addr, first_offset);
    uint64_t pg_pointer = llc_get_entry_record_pages_table(page_addr);
    uint64_t berti_confidence = 0;
    int current_berti = llc_get_berti_current_pages_table(index, berti_confidence);
    uint64_t match_confidence = 0;
    
    // If match with current page+first_offset, use record
    if (pgo_pointer != LLC_RECORD_PAGES_TABLE_ENTRIES
    	&& (llc_record_pages_table[pgo_pointer].u_vector | llc_current_pages_table[index].u_vector) == llc_record_pages_table[pgo_pointer].u_vector) {
      u_vector =  llc_record_pages_table[pgo_pointer].u_vector;
      berti =  llc_record_pages_table[pgo_pointer].berti;
      match_confidence = 1; // High confidence
      recorded = true;
    } else
    // If match with current ip+first_offset, use record
    if (llc_record_pages_table[ip_pointer].first_offset == first_offset
    	&& (llc_record_pages_table[ip_pointer].u_vector | llc_current_pages_table[index].u_vector) == llc_record_pages_table[ip_pointer].u_vector) {
      u_vector =  llc_record_pages_table[ip_pointer].u_vector;
      berti =  llc_record_pages_table[ip_pointer].berti;
      match_confidence = 1; // High confidence
      recorded = true;
    } else
    // If no exact match, trust current if it has already a berti
    if (current_berti != 0 && berti_confidence >= LLC_BERTI_CTR_MED_HIGH_CONFIDENCE) { // Medium-High confidence
      u_vector =  llc_current_pages_table[index].u_vector;
      berti = current_berti;
    } else
    // If match with current page, use record
    if (pg_pointer != LLC_RECORD_PAGES_TABLE_ENTRIES) { // Medium confidence
      u_vector =  llc_record_pages_table[pg_pointer].u_vector;
      berti =  llc_record_pages_table[pg_pointer].berti;
      recorded = true;
    } else
    // If match with current ip, use record
    if (llc_record_pages_table[ip_pointer].u_vector) { // Medium confidence
      u_vector =  llc_record_pages_table[ip_pointer].u_vector;
      berti =  llc_record_pages_table[ip_pointer].berti;
      recorded = true;
    }
    
    // Burst for the first access of a page or if pending bursts
    if (first_offset == offset || llc_current_pages_table[index].last_burst != 0) {
      uint64_t first_burst;
      if (llc_current_pages_table[index].last_burst != 0) {
	first_burst = llc_current_pages_table[index].last_burst;
	llc_current_pages_table[index].last_burst = 0;
      } else if (berti >= 0) {
	first_burst = offset + 1;
      }	else {
	first_burst = offset - 1;
      }
      if (recorded && match_confidence) {
	int bursts = 0;
	if (berti > 0) {
	  for (uint64_t i = first_burst; i < offset+berti; i++) {
	    if (i >= LLC_PAGE_BLOCKS) break; // Stay in the page
	    // Only if previously requested and not demanded
	    uint64_t pf_line_addr = (page_addr << LLC_PAGE_BLOCKS_BITS) | i;
	    uint64_t pf_addr = pf_line_addr << LOG2_BLOCK_SIZE;
	    uint64_t pf_page_addr = pf_line_addr >> LLC_PAGE_BLOCKS_BITS;
	    uint64_t pf_offset = pf_line_addr & LLC_PAGE_OFFSET_MASK;
	    if ((((uint64_t)1 << i) & u_vector)
		&& !llc_requested_offset_current_pages_table(index, pf_offset)) {
	      // if (PQ.occupancy < PQ.SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES) {
        if (internal_PQ.size() < PQ_SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES){
		bool prefetched = prefetch_line(ip, addr, pf_addr, FILL_LLC, 0);
		if (prefetched) {
		  // assert(pf_page_addr == page_addr);
		  llc_add_prev_prefetches_table(index, pf_offset, current_core_cycle[cpu]);
		  bursts++;
		}
	      } else { // record last burst
		llc_current_pages_table[index].last_burst = i;
		break;
	      }
	    }
	  }
	} else if (berti < 0) {
	  for (int i = first_burst; i > ((int)offset)+berti; i--) {
	    if (i < 0) break; // Stay in the page
	    // Only if previously requested and not demanded
	    uint64_t pf_line_addr = (page_addr << LLC_PAGE_BLOCKS_BITS) | i;
	    uint64_t pf_addr = pf_line_addr << LOG2_BLOCK_SIZE;
	    uint64_t pf_page_addr = pf_line_addr >> LLC_PAGE_BLOCKS_BITS;
	    uint64_t pf_offset = pf_line_addr & LLC_PAGE_OFFSET_MASK;
	    if ((((uint64_t)1 << i) & u_vector)
		&& !llc_requested_offset_current_pages_table(index, pf_offset)) {
	      // if (PQ.occupancy < PQ.SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES) {
        if (internal_PQ.size() < PQ_SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES){
		bool prefetched = prefetch_line(ip, addr, pf_addr, FILL_LLC, 0);
		if (prefetched) {
		  // assert(pf_page_addr == page_addr);
		  llc_add_prev_prefetches_table(index, pf_offset, current_core_cycle[cpu]);
		  bursts++;
		}
	      } else { // record last burst
		llc_current_pages_table[index].last_burst = i;
		break;
	      }
	    }
	  }
	} else { // berti == 0 (zig zag of all)
	  for (int i = first_burst, j = (first_offset << 1) - i;
	       i < LLC_PAGE_BLOCKS || j >= 0; i++, j = (first_offset << 1) - i) {
	    // Only if previously requested and not demanded
	    // Dir ++
	    uint64_t pf_line_addr = (page_addr << LLC_PAGE_BLOCKS_BITS) | i;
	    uint64_t pf_addr = pf_line_addr << LOG2_BLOCK_SIZE;
	    uint64_t pf_page_addr = pf_line_addr >> LLC_PAGE_BLOCKS_BITS;
	    uint64_t pf_offset = pf_line_addr & LLC_PAGE_OFFSET_MASK;
	    if ((((uint64_t)1 << i) & u_vector)
		&& !llc_requested_offset_current_pages_table(index, pf_offset)) {
	      // if (PQ.occupancy < PQ.SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES) {
        if (internal_PQ.size() < PQ_SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES){
		bool prefetched = prefetch_line(ip, addr, pf_addr, FILL_LLC, 0);
		if (prefetched) {
		  // assert(pf_page_addr == page_addr);
		  llc_add_prev_prefetches_table(index, pf_offset, current_core_cycle[cpu]);
		  bursts++;
		}
	      } else { // record last burst
		llc_current_pages_table[index].last_burst = i;
		break;
	      }
	    }
	    // Dir --
	    pf_line_addr = (page_addr << LLC_PAGE_BLOCKS_BITS) | j;
	    pf_addr = pf_line_addr << LOG2_BLOCK_SIZE;
	    pf_page_addr = pf_line_addr >> LLC_PAGE_BLOCKS_BITS;
	    pf_offset = pf_line_addr & LLC_PAGE_OFFSET_MASK;
	    if ((((uint64_t)1 << j) & u_vector)
		&& !llc_requested_offset_current_pages_table(index, pf_offset)) {
	      // if (PQ.occupancy < PQ.SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES) {
        if (internal_PQ.size() < PQ_SIZE && bursts < LLC_MAX_NUM_BURST_PREFETCHES){
		bool prefetched = prefetch_line(ip, addr, pf_addr, FILL_LLC, 0);
		if (prefetched) {
		  // assert(pf_page_addr == page_addr);
		  llc_add_prev_prefetches_table(index, pf_offset, current_core_cycle[cpu]);
		  bursts++;
		}
	      } else {
		// record only positive burst
	      }
	    }
	  }
	}  
      } else { // not recorded
      }	
    }
      
    if (berti != 0) {
      uint64_t pf_line_addr = line_addr + berti;
      uint64_t pf_addr = pf_line_addr << LOG2_BLOCK_SIZE;
      uint64_t pf_page_addr = pf_line_addr >> LLC_PAGE_BLOCKS_BITS;
      uint64_t pf_offset = pf_line_addr & LLC_PAGE_OFFSET_MASK;
      if (!llc_requested_offset_current_pages_table(index, pf_offset) // Only prefetch if not demanded
	  && (!match_confidence || (((uint64_t)1 << pf_offset) & u_vector))) { // And prev. accessed
	bool prefetched = prefetch_line(ip, addr, pf_addr, FILL_LLC, 0);
	if (prefetched) {
	  // assert(pf_page_addr == page_addr);
	  llc_add_prev_prefetches_table(index, pf_offset, current_core_cycle[cpu]);
	}
      }
    }
  }
  
  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  uint64_t line_addr = (addr >> LOG2_BLOCK_SIZE);
  uint64_t page_addr = line_addr >> LLC_PAGE_BLOCKS_BITS;
  uint64_t offset = line_addr & LLC_PAGE_OFFSET_MASK;

  uint64_t pointer_prev = llc_get_current_pages_entry(page_addr);

  if (pointer_prev < LLC_CURRENT_PAGES_TABLE_ENTRIES) { // Not found, not entry in prev requests
    uint64_t pref_latency = llc_get_and_set_latency_prev_prefetches_table(pointer_prev, offset, current_core_cycle[cpu]);
    uint64_t demand_latency = llc_get_latency_prev_requests_table(pointer_prev, offset, current_core_cycle[cpu]);
    
    // First look in prefetcher, since if there is a hit, it is the time the miss started
    // If no prefetch, then its latency is the demand one
    if (pref_latency == 0) {
      pref_latency = demand_latency;
    }     
    
    if (demand_latency != 0) { // Not found, berti will not be found neither
      
      // Find berti (distance from pref_latency + demand_latency cycles before
      int berti[LLC_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS]; 
      llc_get_berti_prev_requests_table(pointer_prev, offset, current_core_cycle[cpu] - (pref_latency + demand_latency), berti);
      for (int i = 0; i < LLC_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS; i++) {
	if (berti[i] == 0) break;
	assert(abs(berti[i]) < LLC_PAGE_BLOCKS);
	llc_add_berti_current_pages_table(pointer_prev, berti[i]);
      }
    }
  }

  uint64_t victim_index = llc_get_current_pages_entry(evicted_addr >> LOG2_PAGE_SIZE);
  if (victim_index < LLC_CURRENT_PAGES_TABLE_ENTRIES) {
    // Copy victim to record table
    llc_record_current_page(victim_index);
    llc_remove_current_table_entry(victim_index);
  }
  
  return metadata_in;
}

void CACHE::prefetcher_final_stats()
{
    cout << "CPU " << cpu << " LLC berti prefetcher final stats" << endl;
}