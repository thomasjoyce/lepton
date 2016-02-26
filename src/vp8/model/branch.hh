#ifndef _BRANCH_HH_
#define _BRANCH_HH_
#include "numeric.hh"
#include <cmath>
typedef uint8_t Probability;

//#define VP8_ENCODER 1

//#define JPEG_ENCODER
// ^^^ if we want to try to use the JPEG spec arithmetic coder, uncomment above
class Branch
{
private:
  uint8_t counts_[2];
  Probability probability_;
  friend class JpegBoolDecoder;
  friend class JpegBoolEncoder;
public:
  Probability prob() const { return probability_; }
  void set_identity() {
    counts_[0] = 1;
    counts_[1] = 1;
    probability_ = 128;
  }
  bool is_identity() const {
    return counts_[0] == 1 && counts_[1] == 1 && probability_ == 128;
  }
  static Branch identity() {
    Branch retval;
    retval.set_identity();
    return retval;
  }
  uint32_t true_count() const { return counts_[1]; }
  uint32_t false_count() const { return counts_[0]; }
    struct ProbUpdate {
        struct ProbOutcome {
            uint8_t log_prob;
        };
        uint8_t prob;
        ProbOutcome next[2];
        uint8_t& log_prob_false() {
            return next[0].log_prob;
        }
        uint8_t& log_prob_true() {
            return next[1].log_prob;
        }
    };
    static ProbUpdate update_lookup[4][256];
    static constexpr double log_base = .9575;

    static uint8_t compute_prob_from_log_prob(uint8_t log_prob) {
        uint8_t pval = log_prob >= 128 ? log_prob - 128 : 127 - log_prob;
        uint8_t retval = (uint8_t)(128 * pow(log_base, pval));
        if (log_prob >= 128) {
            return 255 - retval;
        } else {
            return retval;
        }
    }
    static ProbUpdate update_from_log_prob(uint8_t log_prob) {
        ProbUpdate retval = {0, {{0},{0}}};
        int limit = 120;
        if (log_prob == 127) {
            retval.log_prob_false() = 128;
        } else if (log_prob > 127){
            if (log_prob < 128 + 40) {
                retval.log_prob_false() = std::min(log_prob + 1, 255);
            } else if (log_prob < 128 + 96) {
                retval.log_prob_false() = std::min(log_prob + 2, 255);
            } else {
                retval.log_prob_false() = std::min(log_prob + 3, 255);
            }
            if (retval.log_prob_false() > 127 + limit) retval.log_prob_false() = 127 + limit;
        }
        if (log_prob == 128) {
            retval.log_prob_true() = 127;
        } else if (log_prob < 128){
            if (log_prob > 127 - 40) {
                retval.log_prob_true() = std::max(log_prob - 1, 0);
            } else if (log_prob > 127 - 96) {
                retval.log_prob_true() = std::max(log_prob - 2, 0);
            } else {
                retval.log_prob_true() = std::max(log_prob - 3, 0);
            }
            if (retval.log_prob_true() < 128 - limit) {
                retval.log_prob_true() = 128 - limit;
            }
        }
        retval.prob = compute_prob_from_log_prob(log_prob);
        if (log_prob != 127 && log_prob != 128) {
            bool obs = log_prob > 128; // we're assuming a counteraction
            uint8_t pval = obs ? log_prob - 128 : 127 - log_prob;
            double prob = 0.5 * pow(log_base, (double)pval);
            double new_prob = log_base * prob + 1 - log_base;
            int search_result = pval - 1;
            double best_search_dist = fabs(0.5 * pow(log_base, (double)search_result) - new_prob);
            for (int search = search_result - 1; search > 0; --search) {
                double search_prob = 0.5 * pow(log_base, (double)search);
                if (fabs(search_prob - new_prob) < best_search_dist) {
                    search_result =  search;
                    best_search_dist = fabs(search_prob - new_prob);
                } else break;
            }
            if (obs) {
                retval.log_prob_true() = search_result + 128;
            } else {
                retval.log_prob_false() = 127 - search_result;
            }
        }
        if (retval.log_prob_true() & 0x1) {
            //retval.log_prob_true() -= 1;
        }
        if (!(retval.log_prob_false() & 0x1)) {
            //retval.log_prob_false() += 1;
        }
        return retval;
    }
    static void print_prob_update() {
        fprintf(stderr, "unsigned char prob_update_table[256][2][2] = {");
        for (int i = 0; i < 256; ++i) {
            auto table = update_from_log_prob(i);
            fprintf(stderr,"    {0x%x,{{0x%x}, {0x%x}}}%s",
                    (int)table.prob,
                    (int)table.log_prob_false(), (int)table.log_prob_true(),
                    (i == 255 ? "\n};\n" : ",\n"));
        }
    }
  __attribute__((always_inline))
  void record_obs_and_update(bool obs) {
      /*
      static bool pr = true;
      if (pr) {
          pr = false;
          print_prob_update();
          }*/
      unsigned int fcount = counts_[0];
      unsigned int tcount = counts_[1];
      bool overflow = (counts_[obs]++ == 0xff);
      if (__builtin_expect(overflow, 0)) { // check less than 512
          bool neverseen = counts_[!obs] == 1;
          if (neverseen) {
              counts_[obs] = 0xff;
              probability_ = obs ? 0 : 255;
          } else {
              counts_[0] = ((1 + (unsigned int)fcount) >> 1);
              counts_[1] = ((1 + (unsigned int)tcount) >> 1);
              counts_[obs] = 129;
              probability_ = optimize(counts_[0] + counts_[1]);
          }
      } else {
          probability_ = optimize(fcount + tcount + 1);
      }
  }
  void normalize() {
      counts_[0] = ((1 + (unsigned int)counts_[0]) >> 1);
      counts_[1] = ((1 + (unsigned int)counts_[1]) >> 1);
  }
  __attribute__((always_inline))
  Probability optimize(int sum) const
  {
    assert(false_count() && true_count());
#if 0
      const int prob = (false_count() << 8) / sum;
#else
      const int prob = fast_divide18bit_by_10bit(false_count() << 8,
                                        sum);
#endif
      assert( prob >= 0 );
      assert( prob <= 255 );
      
      return (Probability)prob;

#ifdef JPEG_ENCODER
#error needs to be updated
#endif
  }

  Branch(){}
};
#endif
