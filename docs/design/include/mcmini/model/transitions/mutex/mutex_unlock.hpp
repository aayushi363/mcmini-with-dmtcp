#pragma once

#include "mcmini/model/objects/mutex.hpp"
#include "mcmini/model/transition.hpp"

namespace model {
namespace transitions {

struct mutex_unlock : public model::transition {
 private:
  state::objid_t mutex_id; /* The mutex this transition unlocks */

 public:
  mutex_unlock(runner_id_t executor, state::objid_t mutex_id)
      : mutex_id(mutex_id), transition(executor) {}
  ~mutex_unlock() = default;

  status modify(model::mutable_state& s) const override {
    using namespace model::objects;
    const mutex* ms = s.get_state_of_object<mutex>(mutex_id);
    s.add_state_for_obj(mutex_id, mutex::make(mutex::unlocked));
    return status::exists;
  }

  std::string to_string() const override {
    return "mutex_unlock(mutex:" + std::to_string(mutex_id) + ")";
  }
};
}  // namespace transitions
}  // namespace model