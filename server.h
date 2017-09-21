/**
 * @file server.h
 * @author Simon Prykhodko
 * @brief Synchronizing server implementation.
 */

#ifndef SYNCER_SERVER_H_
#define SYNCER_SERVER_H_

#include <mutex>

#include "backend.h"
#include "json.hpp"

namespace syncer {

using namespace std;

/**
 * @brief Synchronizing server.
 * @details It creates both replier and publisher at time of construction.
 * The replier answers client requests providing a full data state. If data is
 * changed the publisher sends JSON patch (RFC 6902) which is applied on a
 * client side.
 *
 * Requirements for data type template parameter:
 *   - Must have a default constructor.
 *   - Must have `from_json` and `to_json` function overloads.
 *   - Might have a move constructor (can boost performance).
 */
template <typename T, typename Backend = DefaultBackend> class Server {
 public:
  /**
   * @brief Constructor.
   * @param rep_conf a replier configuration.
   * @param pub_conf a publisher configuration.
   * @param data an initial data state.
   */
  Server(const typename Backend::Config& rep_conf,
         const typename Backend::Config& pub_conf,
         const T& data)
      : rep_(rep_conf, bind(&Server::HandleRequest, ref(*this), _1))
      , pub_(pub_conf) {
    to_json(state_, data);
    state_[VERSION_KEY] = ver_;

    {
      lock_guard<mutex> _(mtx_);
      reply_ = state_.dump();
    }

    // empty message means to request a full state
    pub_.Publish(Message());
  }

  /**
   * @brief Update data state.
   * @param data a new data state.
   */
  void Update(const T& data) {
    json next;
    to_json(next, data);
    next[VERSION_KEY] = ver_ + 1;

    auto diff = json::diff(state_, next);
    if (diff.size() > 1) {
      state_ = next;
      ver_++;

      {
        lock_guard<mutex> _(mtx_);
        reply_ = state_.dump();
      }

      pub_.Publish(diff.dump());
    }
  }

 private:
  static constexpr char const * VERSION_KEY = "__syncer_data_version";

  Message HandleRequest(const Message&) {
    lock_guard<mutex> _(mtx_);
    return reply_;
  }

  typename Backend::Replier rep_;
  typename Backend::Publisher pub_;
  json state_;
  int ver_ = 0;
  Message reply_;
  mutex mtx_;
};

}

#endif // SYNCER_SERVER_H_
