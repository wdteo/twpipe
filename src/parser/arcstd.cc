#include "arcstd.h"
#include "twpipe/logging.h"
#include "twpipe/corpus.h"
#include "twpipe/alphabet_collection.h"
#include <bitset>
#include <boost/assert.hpp>
#include <boost/multi_array.hpp>

namespace twpipe {

ArcStandard::ArcStandard() {
  Alphabet & map = AlphabetCollection::get()->deprel_map;
  n_actions = 1 + 2 * map.size();

  action_names.push_back("SHIFT");
  for (unsigned i = 0; i < map.size(); ++i) {
    action_names.push_back("LEFT-" + map.get(i));
    action_names.push_back("RIGHT-" + map.get(i));
  }
  _INFO << "[parse|arcstd] show action names:";
  for (const auto& action_name : action_names) {
    _INFO << "- " << action_name;
  }
}

std::string twpipe::ArcStandard::name() const {
  return "arcstd";
}

std::string ArcStandard::name(unsigned id) const {
  BOOST_ASSERT_MSG(id < action_names.size(), "id in illegal range");
  return action_names[id];
}

bool ArcStandard::allow_nonprojective() const {
  return false;
}

unsigned ArcStandard::num_actions() const { return n_actions; }

void ArcStandard::shift_unsafe(State& state) const {
  state.stack.push_back(state.buffer.back());
  state.buffer.pop_back();
}

void ArcStandard::left_unsafe(State& state,
                              const unsigned& deprel) const {
  unsigned hed = state.stack.back(); state.stack.pop_back();
  unsigned mod = state.stack.back(); state.stack.back() = hed;
  state.heads[mod] = hed;
  state.deprels[mod] = deprel;
}

void ArcStandard::right_unsafe(State& state,
                               const unsigned& deprel) const {
  unsigned mod = state.stack.back(); state.stack.pop_back();
  unsigned hed = state.stack.back();
  state.heads[mod] = hed;
  state.deprels[mod] = deprel;
}

unsigned ArcStandard::cost(const State& state,
                           const std::vector<unsigned>& ref_heads,
                           const std::vector<unsigned>& ref_deprels) const {
  // TODO: since twpipe left-rooted, we need re-write the cost function, but it's yet be done.
  // handling the initial state.
  // ref_heads is counted as [0, ... , N], the index of the first legal word is 0.
  // there is a guard in state.stack and state.buffer and the indices in the state
  // is counted as [0, ..., N], N is the root.
  const std::vector<unsigned>& stack = state.stack;
  const std::vector<unsigned>& buffer = state.buffer;

  if (stack.size() == 1) { return 0; }
  std::vector< std::vector<unsigned> > tree(ref_heads.size());

  unsigned root = 0;
  for (unsigned i = 0; i < ref_heads.size(); ++i) {
    unsigned h = ref_heads[i];
    if (h != Corpus::BAD_HED) {
      tree[h].push_back(i);
    } else {
      root = i;
    }
  }

  std::vector<unsigned> sigma_l;
  for (unsigned i = stack.size() - 1; i > 0; --i) { sigma_l.push_back(stack[i]); }
  std::vector<unsigned> sigma_r; sigma_r.push_back(sigma_l.back());

  // constructing sigma_r
  std::bitset<State::MAX_N_WORDS> sigma_l_mask;
  std::bitset<State::MAX_N_WORDS> sigma_r_mask;
  for (auto s : sigma_l) { sigma_l_mask.set(s); }

  unsigned buffer_front = buffer.back();
  for (unsigned i = buffer.size() - 1; i > 0; --i) {
    unsigned id = buffer[i];
    // the parent node of i in t_G is not in \beta;
    if (ref_heads[id] < buffer_front || ref_heads[id] == Corpus::BAD_HED) {
      sigma_r.push_back(id); sigma_r_mask.set(id); continue;
    }
    // some dependent of i in t_G is in \sigma_L or has already been inserted in \sigma_R.
    for (auto d : tree[id]) {
      if (sigma_l_mask.test(d) || sigma_r_mask.test(d)) {
        sigma_r.push_back(id); sigma_r_mask.set(id); break;
      }
    }
  }

  // compute the loss.
  unsigned len = ref_heads.size();
  int len_l = static_cast<int>(sigma_l.size());
  int len_r = static_cast<int>(sigma_r.size());

  typedef boost::multi_array<unsigned, 3> array_t;
  array_t T(boost::extents[len_l][len_r][len]);
  std::fill(T.origin(), T.origin() + T.num_elements(), 100000);

  int h = sigma_l[0];
  T[0][0][h] = 0;
  for (int d = 1; d <= len_l + len_r - 1; ++d) {
    for (int j = std::max(0, d - len_l); j < std::min(d, len_r); ++j) {
      int i = d - j - 1;
      if (i < len_l - 1) {
        unsigned i_1 = sigma_l[i + 1];
        for (auto rank = 0; rank <= i; ++rank) {
          auto h = sigma_l[rank];
          T[i + 1][j][h] = std::min(T[i + 1][j][h], T[i][j][h] + (ref_heads[i_1] == h ? 0 : 1));
          T[i + 1][j][i_1] = std::min(T[i + 1][j][i_1], T[i][j][h] + (ref_heads[h] == i_1 ? 0 : 1));
        }
        for (auto rank = 0; rank <= j; ++rank) {
          auto h = sigma_r[rank];
          T[i + 1][j][h] = std::min(T[i + 1][j][h], T[i][j][h] + (ref_heads[i_1] == h ? 0 : 1));
          T[i + 1][j][i_1] = std::min(T[i + 1][j][i_1], T[i][j][h] + (ref_heads[h] == i_1 ? 0 : 1));
        }
      }

      if (j < len_r - 1) {
        unsigned j_1 = sigma_r[j + 1];
        for (auto rank = 0; rank <= i; ++rank) {
          auto h = sigma_l[rank];
          T[i][j + 1][h] = std::min(T[i][j + 1][h], T[i][j][h] + (ref_heads[j_1] == h ? 0 : 1));
          T[i][j + 1][j_1] = std::min(T[i][j + 1][j_1], T[i][j][h] + (ref_heads[h] == j_1 ? 0 : 1));
        }
        for (auto rank = 0; rank <= j; ++rank) {
          auto h = sigma_r[rank];
          T[i][j + 1][h] = std::min(T[i][j + 1][h], T[i][j][h] + (ref_heads[j_1] == h ? 0 : 1));
          T[i][j + 1][j_1] = std::min(T[i][j + 1][j_1], T[i][j][h] + (ref_heads[h] == j_1 ? 0 : 1));
        }
      }
    }
  }
  auto penalty = 0;
  for (unsigned i = 0; i < std::min(buffer_front, len); ++i) {
    if (state.heads[i] != Corpus::BAD_HED) {
      if (state.heads[i] != ref_heads[i] || state.deprels[i] != ref_deprels[i]) {
        penalty += 1;
      }
    }
  }
  // note, ROOT is not zero but last one.
  return T[len_l - 1][len_r - 1][root] + penalty;
}

void ArcStandard::get_transition_costs(const State & state,
                                       const std::vector<unsigned>& actions,
                                       const std::vector<unsigned>& ref_heads,
                                       const std::vector<unsigned>& ref_deprels,
                                       std::vector<float>& costs) {
  float c = static_cast<float>(cost(state, ref_heads, ref_deprels));
  float wrong_left = -1e8f, wrong_right = -1e8f;
  costs.clear();

  for (unsigned act : actions) {
    if (is_shift(act)) {
      State next_state(state);
      perform_action(next_state, act);
      costs.push_back(c - cost(next_state, ref_heads, ref_deprels));
    } else if (is_left(act)) {
      unsigned deprel = parse_label(act), hed = state.stack.back(), mod = state.stack[state.stack.size() - 2];
      if (ref_heads[mod] == hed && ref_deprels[mod] == deprel) {
        // assume that actions are unique and there is only one correct left action.
        State next_state(state); perform_action(next_state, act);
        costs.push_back(c - cost(next_state, ref_heads, ref_deprels));
      } else if (wrong_left == -1e8) {
        State next_state(state); perform_action(next_state, act);
        wrong_left = c - cost(next_state, ref_heads, ref_deprels);
        costs.push_back(wrong_left);
      } else {
        costs.push_back(wrong_left);
      }
    } else {
      unsigned deprel = parse_label(act), mod = state.stack.back(), hed = state.stack[state.stack.size() - 2];
      if (ref_heads[mod] == hed && ref_deprels[mod] == deprel) {
        State next_state(state); perform_action(next_state, act);
        costs.push_back(c - cost(next_state, ref_heads, ref_deprels));
      } else if (wrong_right == -1e8) {
        State next_state(state); perform_action(next_state, act);
        wrong_right = c - cost(next_state, ref_heads, ref_deprels);
        costs.push_back(wrong_right);
      } else {
        costs.push_back(wrong_right);
      }
    }
  }
}

unsigned ArcStandard::get_structure_action(const unsigned & action) {
  return (action < 1 ? action : (action % 2 == 1 ? 1 : 2));
}

void ArcStandard::perform_action(State & state, const unsigned& action) {
  if (is_shift(action)) {
    // SHITF: counting for the last GUARD
    shift_unsafe(state);
  } else {
    // LEFT or RIGHT: counting for the begining GUARD
    unsigned lid = parse_label(action);
    if (is_left(action)) {
      left_unsafe(state, lid);
    } else {
      right_unsafe(state, lid);
    }
  }
}

unsigned ArcStandard::get_shift_id() const { return 0; }
unsigned ArcStandard::get_left_id(const unsigned& deprel) const { return deprel * 2 + 1; }
unsigned ArcStandard::get_right_id(const unsigned& deprel) const { return deprel * 2 + 2; }

bool ArcStandard::is_shift(const unsigned& action) { return action == 0; }
bool ArcStandard::is_left(const unsigned& action) { return action % 2 == 1; }
bool ArcStandard::is_right(const unsigned& action) { return (action > 1 && action % 2 == 0); }

bool ArcStandard::is_valid_action(const State& state, const unsigned& act) const {
  if (is_shift(act)) {
    if (state.buffer.size() == 1) { return false; }
  } else {
    if (state.stack.size() < 3) { return false; }
    /* should not left the root. */
    if (is_left(act) && state.stack[state.stack.size() - 2] == 0) { return false; }
  }
  return true;
}

void ArcStandard::get_valid_actions(const State& state, std::vector<unsigned>& valid_actions) {
  valid_actions.clear();
  for (unsigned a = 0; a < n_actions; ++a) {
    //if (!is_valid_action(state, action_names[a])) { continue; }
    if (!is_valid_action(state, a)) { continue; }
    valid_actions.push_back(a);
  }
  BOOST_ASSERT_MSG(valid_actions.size() > 0, "There should be one or more valid action.");
}

unsigned ArcStandard::parse_label(const unsigned& action) const {
  BOOST_ASSERT_MSG(action > 0, "SHIFT do not have label.");
  return (action - 1) / 2;
}

void ArcStandard::get_oracle_actions(const std::vector<unsigned>& heads,
                                     const std::vector<unsigned>& deprels,
                                     std::vector<unsigned>& actions) {
  actions.clear();
  auto len = heads.size();
  std::vector<unsigned> sigma;
  std::vector<unsigned> output(len, -1);
  unsigned beta = 0;

  while (!(sigma.size() == 1 && beta == len)) {
    get_oracle_actions_onestep(heads, deprels, sigma, beta, output, actions);
  }
}

void ArcStandard::get_oracle_actions_onestep(const std::vector<unsigned>& heads,
                                             const std::vector<unsigned>& deprels,
                                             std::vector<unsigned>& sigma,
                                             unsigned& beta,
                                             std::vector<unsigned>& output,
                                             std::vector<unsigned>& actions) {
  unsigned top0 = (sigma.size() > 0 ? sigma.back() : Corpus::BAD_HED);
  unsigned top1 = (sigma.size() > 1 ? sigma[sigma.size() - 2] : Corpus::BAD_HED);

  bool all_descendents_reduced = true;
  if (top0 != Corpus::BAD_HED) {
    for (unsigned i = 0; i < heads.size(); ++i) {
      if (heads[i] == top0 && output[i] != top0) { all_descendents_reduced = false; break; }
    }
  }

  if (top1 != Corpus::BAD_HED && heads[top1] == top0) {
    actions.push_back(get_left_id(deprels[top1]));
    output[top1] = top0;
    sigma.pop_back();
    sigma.back() = top0;
  } else if (top1 != Corpus::BAD_HED && heads[top0] == top1 && all_descendents_reduced) {
    actions.push_back(get_right_id(deprels[top0]));
    output[top0] = top1;
    sigma.pop_back();
  } else {
    BOOST_ASSERT_MSG(beta < heads.size(), "should be more than one");
    actions.push_back(get_shift_id());
    sigma.push_back(beta);
    ++beta;
  }
}

}