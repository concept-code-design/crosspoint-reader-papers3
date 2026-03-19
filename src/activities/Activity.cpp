#include "Activity.h"

#include "ActivityManager.h"

void Activity::onEnter() {
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
  mappedInput.clearState();  // Prevent stale touches from triggering actions in the new activity
#if CROSSPOINT_PAPERS3
  renderer.requestFullRefresh();  // Full e-ink refresh on every activity transition to prevent ghosting
#endif
}

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome() { activityManager.goHome(); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
