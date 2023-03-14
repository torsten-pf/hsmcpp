// Copyright (C) 2021 Igor Krechetov
// Distributed under MIT license. See file LICENSE for details

#ifndef HSMCPP_HSMEVENTDISPATCHERGLIB_HPP
#define HSMCPP_HSMEVENTDISPATCHERGLIB_HPP

#include "HsmEventDispatcherBase.hpp"
#include <glib.h>
#include <condition_variable>
#include <map>

namespace hsmcpp
{

/**
 * @brief HsmEventDispatcherGLib provides dispatcher implementation based on glib library.
 * @details Events queue is implemented by using glib IO channel. See @rstref{platforms-dispatcher-glib} for details.
 */
class HsmEventDispatcherGLib: public HsmEventDispatcherBase
{
private:
    using TimerData_t = std::pair<HsmEventDispatcherGLib*, TimerID_t>;

public:
    /**
     * @copydoc HsmEventDispatcherBase::HsmEventDispatcherBase()
     * @details Uses default glib context.
    */
    explicit HsmEventDispatcherGLib(const size_t eventsCacheSize = DISPATCHER_DEFAULT_EVENTS_CACHESIZE);

    /**
     * @copydoc HsmEventDispatcherBase::HsmEventDispatcherBase()
     * @param context custom glib context to use for dispatcher.
    */
    HsmEventDispatcherGLib(GMainContext* context, const size_t eventsCacheSize = DISPATCHER_DEFAULT_EVENTS_CACHESIZE);

    /**
     * Destructor.
     */
    virtual ~HsmEventDispatcherGLib();

    /**
     * @brief Create a new IO channel and start events dispatching.
     * @details See IHsmEventDispatcher::start() for details.
     * @notthreadsafe{Thread safety is not required by HierarchicalStateMachine::initialize() which uses this API.}
     */
    bool start() override;

    /**
     * @brief See IHsmEventDispatcher::emitEvent()
     * @threadsafe{ }
     */
    void emitEvent(const HandlerID_t handlerID) override;

private:
    void unregisterAllTimerHandlers();

    void startTimerImpl(const TimerID_t timerID, const unsigned int intervalMs, const bool isSingleShot) override;
    void stopTimerImpl(const TimerID_t timerID) override;

    static gboolean onTimerEvent(const TimerData_t* timerData);
    static void onFreeTimerData(void* timerData);

    void notifyDispatcherAboutEvent() override;
    static gboolean onPipeDataAvailable(GIOChannel* gio, GIOCondition condition, gpointer data);

private:
    GMainContext* mContext = nullptr;
    GIOChannel* mReadChannel = nullptr;
    GSource* mIoSource = nullptr;
    std::mutex mPipeSync;
    int mPipeFD[2] = {-1, -1};
    bool mStopDispatcher = false;
    bool mDispatchingIterationRunning = false;
    std::mutex mDispatchingSync;
    std::condition_variable mDispatchingDoneEvent;
    std::map<TimerID_t, GSource*> mNativeTimerHandlers;// <timerID, nativeTimerID>
};

} // namespace hsmcpp

#endif // HSMCPP_HSMEVENTDISPATCHERGLIB_HPP
