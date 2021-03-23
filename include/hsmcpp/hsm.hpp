// Copyright (C) 2021 Igor Krechetov
// Distributed under MIT license. See file LICENSE for details

#ifndef __HSMCPP_HSM_HPP__
#define __HSMCPP_HSM_HPP__

#include <algorithm>
#include <functional>
#include <atomic>
#include <map>
#include <list>
#include <vector>
#include <mutex>
#include <condition_variable>
#include "IHsmEventDispatcher.hpp"
#include "variant.hpp"
#include "logging.hpp"

// If defined, HSM will performe safety checks during states and substates registration.
// Normally HSM structure should be static, so this feature is usefull only
// during development since it reduces performance a bit
// #define HSM_ENABLE_SAFE_STRUCTURE                    1

// Thread safety is enabled by default, but it adds some overhead related with mutex usage.
// If performance is critical and it's ensured that HSM is used only from a single thread,
// then synchronization could be disabled during compilation.
// #define HSM_DISABLE_THREADSAFETY                     1

#ifdef HSM_DISABLE_THREADSAFETY
  #define _HSM_SYNC_EVENTS_QUEUE()
#else
  #define _HSM_SYNC_EVENTS_QUEUE()                std::lock_guard<std::mutex> lck(mEventsSync)
#endif // HSM_DISABLE_THREADSAFETY

#undef __TRACE_CLASS__
#define __TRACE_CLASS__                         "HierarchicalStateMachine"

#define HSM_WAIT_INDEFINITELY                   (0)

typedef std::vector<Variant> VariantList_t;

template <typename HsmStateEnum, typename HsmEventEnum>
class HierarchicalStateMachine
{
public:
    typedef std::function<void(const VariantList_t&)> HsmTransitionCallback_t;
    typedef std::function<bool(const VariantList_t&)> HsmTransitionConditionCallback_t;
    typedef std::function<void(const VariantList_t&)> HsmStateChangedCallback_t;
    typedef std::function<bool(const VariantList_t&)> HsmStateEnterCallback_t;
    typedef std::function<bool(void)>                 HsmStateExitCallback_t;

    #define HsmTransitionCallbackPtr_t(_class, _func)              void (_class::*_func)(const VariantList_t&)
    #define HsmTransitionConditionCallbackPtr_t(_class, _func)     bool (_class::*_func)(const VariantList_t&)
    #define HsmStateChangedCallbackPtr_t(_class, _func)            void (_class::*_func)(const VariantList_t&)
    #define HsmStateEnterCallbackPtr_t(_class, _func)              bool (_class::*_func)(const VariantList_t&)
    #define HsmStateExitCallbackPtr_t(_class, _func)               bool (_class::*_func)()

private:
    enum class HsmEventStatus
    {
        PENDING,
        DONE_OK,
        DONE_FAILED
    };

    // NOTE: just an alias to make code more readable
    #define HsmEventStatus_t   HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::HsmEventStatus

    struct StateCallbacks
    {
        HsmStateChangedCallback_t onStateChanged = nullptr;
        HsmStateEnterCallback_t onEntering = nullptr;
        HsmStateExitCallback_t onExiting = nullptr;
    };

    struct TransitionInfo
    {
        HsmStateEnum destinationState;
        HsmTransitionCallback_t onTransition = nullptr;
        HsmTransitionConditionCallback_t checkCondition = nullptr;
    };

    struct PendingEventInfo
    {
        bool entryPointTransition = false;
        HsmEventEnum type = static_cast<HsmEventEnum>(-1);
        VariantList_t args;
        std::shared_ptr<std::mutex> cvLock;
        std::shared_ptr<std::condition_variable> syncProcessed;
        std::shared_ptr<HsmEventStatus> transitionStatus;

        ~PendingEventInfo();
        void initLock();
        void releaseLock();
        bool isSync();
        void wait(const int timeoutMs = HSM_WAIT_INDEFINITELY);
        void unlock(const HsmEventStatus status);
    };

public:
    explicit HierarchicalStateMachine(const HsmStateEnum initialState);
    // Uses unregisterEventHandler from Dispatcher. Usually HSM has to be destroyed from the same thread it was created.
    virtual ~HierarchicalStateMachine();

    // Uses registerEventHandler from Dispatcher. Usually must be called from the same thread where dispatcher was created.
    bool initialize(const std::shared_ptr<IHsmEventDispatcher>& dispatcher);

    // Releases dispatcher and resets all internal resources. HSM cant be reused after calling this API.
    // Must be called on the same thread as initialize()
    //
    // NOTE: Usually you dont need to call this function directly. The only scenario when it's needed is
    //       for multithreaded environment where it's impossible to delete HSM on the same thread where it was initialized.
    //       Then you must call release() on the Dispatcher's thread before deleting HSM instance on another thread.
    void release();

    // If state has substates its callbacks will be ignored
    template <class HsmHandlerClass>
    void registerState(const HsmStateEnum state,
                       HsmHandlerClass* handler = nullptr,
                       HsmStateChangedCallbackPtr_t(HsmHandlerClass, onStateChanged) = nullptr,
                       HsmStateEnterCallbackPtr_t(HsmHandlerClass, onEntering) = nullptr,
                       HsmStateExitCallbackPtr_t(HsmHandlerClass, onExiting) = nullptr);

    void registerState(const HsmStateEnum state,
                       HsmStateChangedCallback_t onStateChanged = nullptr,
                       HsmStateEnterCallback_t onEntering = nullptr,
                       HsmStateExitCallback_t onExiting = nullptr);

    // if multiple entry points are specified only the last one will be applied
    bool registerSubstate(const HsmStateEnum parent, const HsmStateEnum substate, const bool isEntryPoint = false);

    template <class HsmHandlerClass>
    void registerTransition(const HsmStateEnum from,
                            const HsmStateEnum to,
                            const HsmEventEnum onEvent,
                            HsmHandlerClass* handler = nullptr,
                            HsmTransitionCallbackPtr_t(HsmHandlerClass, transitionCallback) = nullptr,
                            HsmTransitionConditionCallbackPtr_t(HsmHandlerClass, conditionCallback) = nullptr);

    void registerTransition(const HsmStateEnum from,
                            const HsmStateEnum to,
                            const HsmEventEnum onEvent,
                            HsmTransitionCallback_t transitionCallback = nullptr,
                            HsmTransitionConditionCallback_t conditionCallback = nullptr);

    HsmStateEnum getCurrentState() const;

    // extended version of transition function with all possible arguments
    // use HSM_WAIT_INDEFINITELY for timeoutMs to ignore timeout
    template <typename... Args>
    bool transitionEx(const HsmEventEnum event, const bool clearQueue, const bool sync, const int timeoutMs, Args... args);

    // basic async transition
    template <typename... Args>
    void transition(const HsmEventEnum event, Args... args);

    // sync transition
    template <typename... Args>
    bool transitionSync(const HsmEventEnum event, const int timeoutMs, Args... args);

    // async transition which clears events queue before adding requested event
    template <typename... Args>
    void transitionWithQueueClear(const HsmEventEnum event, Args... args);

    template <typename... Args>
    bool isTransitionPossible(const HsmEventEnum event, Args... args);

private:
    template <typename... Args>
    void makeVariantList(VariantList_t& vList, Args&&... args);

    void dispatchEvents();

    bool onStateExiting(const HsmStateEnum state);
    bool onStateEntering(const HsmStateEnum state, const VariantList_t& args);
    void onStateChanged(const HsmStateEnum state, const VariantList_t& args);
    bool getParentState(const HsmStateEnum child, HsmStateEnum& outParent);
    bool findTransitionTarget(const HsmEventEnum event,
                              const VariantList_t& transitionArgs,
                              TransitionInfo& outTransition);
    bool findTransitionTarget(const HsmStateEnum fromState,
                              const HsmEventEnum event,
                              const VariantList_t& transitionArgs,
                              TransitionInfo& outTransition);
    HsmEventStatus doTransition(const PendingEventInfo& event);
    void clearPendingEvents();

    bool getEntryPoint(const HsmStateEnum state, HsmStateEnum &outEntryPoint) const;

#ifdef HSM_ENABLE_SAFE_STRUCTURE
    bool isTopState(const HsmStateEnum state) const;
    bool isSubstate(const HsmStateEnum state) const;
    bool hasSubstates(const HsmStateEnum state) const;
    bool hasParentState(const HsmStateEnum state, HsmStateEnum &outParent) const;
#endif // HSM_ENABLE_SAFE_STRUCTURE

private:
    HsmStateEnum mCurrentState;
    std::multimap<std::pair<HsmStateEnum, HsmEventEnum>, TransitionInfo> mTransitionsByEvent; // FROM_STATE, EVENT => TO
    std::map<HsmStateEnum, StateCallbacks> mRegisteredStates;
    std::multimap<HsmStateEnum, HsmStateEnum> mSubstates;
    std::map<HsmStateEnum, HsmStateEnum> mSubstateEntryPoint;
    std::list<PendingEventInfo> mPendingEvents;
    std::shared_ptr<IHsmEventDispatcher> mDispatcher;
    int mDispatcherHandlerId = INVALID_HSM_DISPATCHER_HANDLER_ID;
    bool mStopDispatching = false;

#ifdef HSM_ENABLE_SAFE_STRUCTURE
    std::list<HsmStateEnum> mTopLevelStates; // list of states which are not substates and dont have substates of their own
#endif // HSM_ENABLE_SAFE_STRUCTURE

#ifndef HSM_DISABLE_THREADSAFETY
    std::mutex mEventsSync;
#endif// HSM_DISABLE_THREADSAFETY
};

// ============================================================================
// PUBLIC
// ============================================================================
template <typename HsmStateEnum, typename HsmEventEnum>
HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::HierarchicalStateMachine(const HsmStateEnum initialState)
    : mCurrentState(initialState)
{
    __TRACE_INIT__();
}

template <typename HsmStateEnum, typename HsmEventEnum>
HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::~HierarchicalStateMachine()
{
    release();
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::initialize(const std::shared_ptr<IHsmEventDispatcher>& dispatcher)
{
    __TRACE_CALL_DEBUG__();
    bool result = false;

    if (dispatcher)
    {
        if (true == dispatcher->start())
        {
            mDispatcher = dispatcher;
            mDispatcherHandlerId = mDispatcher->registerEventHandler(std::bind(&HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::dispatchEvents,
                                                                     this));

            result = (INVALID_HSM_DISPATCHER_HANDLER_ID != mDispatcherHandlerId);
        }
        else
        {
            __TRACE_ERROR__("failed to start dispatcher");
        }
    }
    else
    {
        __TRACE_ERROR__("dispatcher is NULL");
    }

    return result;
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::release()
{
    mStopDispatching = true;
    __TRACE_CALL_DEBUG__();

    if (mDispatcher)
    {
        mDispatcher->unregisterEventHandler(mDispatcherHandlerId);
        mDispatcher.reset();
        mDispatcherHandlerId = INVALID_HSM_DISPATCHER_HANDLER_ID;
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
template <class HsmHandlerClass>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::registerState(const HsmStateEnum state,
                                                                         HsmHandlerClass* handler,
                                                                         HsmStateChangedCallbackPtr_t(HsmHandlerClass, onStateChanged),
                                                                         HsmStateEnterCallbackPtr_t(HsmHandlerClass, onEntering),
                                                                         HsmStateExitCallbackPtr_t(HsmHandlerClass, onExiting))
{
    HsmStateChangedCallback_t funcStateChanged;
    HsmStateEnterCallback_t funcEntering;
    HsmStateExitCallback_t funcExiting;

    if (nullptr != handler)
    {
        if (nullptr != onStateChanged)
        {
            funcStateChanged = std::bind(onStateChanged, handler, std::placeholders::_1);
        }

        if (nullptr != onEntering)
        {
            funcEntering = std::bind(onEntering, handler, std::placeholders::_1);
        }

        if (nullptr != onExiting)
        {
            funcExiting = std::bind(onExiting, handler);
        }
    }

    registerState(state, funcStateChanged, funcEntering, funcExiting);
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::registerState(const HsmStateEnum state,
                                                                         HsmStateChangedCallback_t onStateChanged,
                                                                         HsmStateEnterCallback_t onEntering,
                                                                         HsmStateExitCallback_t onExiting)
{
#ifdef HSM_ENABLE_SAFE_STRUCTURE
    if ((false == isSubstate(state)) && (false == isTopState(state)))
    {
        mTopLevelStates.push_back(state);
    }
#endif // HSM_ENABLE_SAFE_STRUCTURE

    if (onStateChanged || onEntering || onExiting)
    {
        StateCallbacks cb;

        cb.onStateChanged = onStateChanged;
        cb.onEntering = onEntering;
        cb.onExiting = onExiting;
        mRegisteredStates[state] = cb;

        __TRACE_CALL_DEBUG_ARGS__("mRegisteredStates.size=%ld", mRegisteredStates.size());
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::registerSubstate(const HsmStateEnum parent,
                                                                            const HsmStateEnum substate,
                                                                            const bool isEntryPoint)
{
    bool registrationAllowed = false;

#ifdef HSM_ENABLE_SAFE_STRUCTURE
    // do a simple sanity check
    if (parent != substate)
    {
        HsmStateEnum curState = parent;
        HsmStateEnum prevState;

        if (false == hasParentState(substate, prevState))
        {
            registrationAllowed = true;

            while (true == hasParentState(curState, prevState))
            {
                if (substate == prevState)
                {
                    __TRACE_CALL_DEBUG_ARGS__("requested operation will result in substates recursion (parent=%d, substate=%d)",
                                              static_cast<int>(parent), static_cast<int>(substate));
                    registrationAllowed = false;
                    break;
                }

                curState = prevState;
            }
        }
        else
        {
            __TRACE_CALL_DEBUG_ARGS__("substate %d already has a parent %d",
                                      static_cast<int>(substate), static_cast<int>(prevState));
        }

        if (true == registrationAllowed)
        {
            auto itEntryPoint = mSubstateEntryPoint.find(parent);

            if ((false == isEntryPoint) && (itEntryPoint == mSubstateEntryPoint.end()))
            {
                __TRACE_CALL_DEBUG_ARGS__("state %d needs to have an entry point defined before a regular substate %d could be added",
                                          static_cast<int>(parent), static_cast<int>(itEntryPoint->second));
                registrationAllowed = false;
            }
            else if ((true == isEntryPoint) && (itEntryPoint != mSubstateEntryPoint.end()))
            {
                __TRACE_CALL_DEBUG_ARGS__("state %d already has an entry point %d",
                                          static_cast<int>(parent), static_cast<int>(itEntryPoint->second));
                registrationAllowed = false;
            }
        }
    }
#else
    registrationAllowed = (parent != substate);
#endif // HSM_ENABLE_SAFE_STRUCTURE

    if (registrationAllowed)
    {
        if (isEntryPoint)
        {
            mSubstateEntryPoint[parent] = substate;
        }

        mSubstates.emplace(parent, substate);

#ifdef HSM_ENABLE_SAFE_STRUCTURE
        if (isTopState(substate))
        {
            mTopLevelStates.remove(substate);
        }
#endif // HSM_ENABLE_SAFE_STRUCTURE
    }

    return registrationAllowed;
}

template <typename HsmStateEnum, typename HsmEventEnum>
template <class HsmHandlerClass>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::registerTransition(const HsmStateEnum from,
                                                                              const HsmStateEnum to,
                                                                              const HsmEventEnum onEvent,
                                                                              HsmHandlerClass* handler,
                                                                              HsmTransitionCallbackPtr_t(HsmHandlerClass, transitionCallback),
                                                                              HsmTransitionConditionCallbackPtr_t(HsmHandlerClass, conditionCallback))
{
    HsmTransitionCallback_t funcTransitionCallback;
    HsmTransitionConditionCallback_t funcConditionCallback;

    if (nullptr != handler)
    {
        if (nullptr != transitionCallback)
        {
            funcTransitionCallback = std::bind(transitionCallback, handler, std::placeholders::_1);
        }

        if (nullptr != conditionCallback)
        {
            funcConditionCallback = std::bind(conditionCallback, handler, std::placeholders::_1);
        }
    }

    registerTransition(from, to, onEvent, funcTransitionCallback, funcConditionCallback);
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::registerTransition(const HsmStateEnum from,
                                                                              const HsmStateEnum to,
                                                                              const HsmEventEnum onEvent,
                                                                              HsmTransitionCallback_t transitionCallback,
                                                                              HsmTransitionConditionCallback_t conditionCallback)
{
    mTransitionsByEvent.emplace(std::make_pair(from, onEvent), TransitionInfo{to, /*handler,*/ transitionCallback, conditionCallback});
}

template <typename HsmStateEnum, typename HsmEventEnum>
inline HsmStateEnum HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::getCurrentState() const
{
    return mCurrentState;
}

template <typename HsmStateEnum, typename HsmEventEnum>
template <typename... Args>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::transitionEx(const HsmEventEnum event,
                                                                        const bool clearQueue,
                                                                        const bool sync,
                                                                        const int timeoutMs,
                                                                        Args... args)
{
    __TRACE_CALL_DEBUG_ARGS__("transitionEx: event=%d, clearQueue=%s, sync=%s", static_cast<int>(event), BOOL2STR(clearQueue), BOOL2STR(sync));

    bool status = false;
    PendingEventInfo eventInfo;

    eventInfo.type = event;
    makeVariantList(eventInfo.args, args...);

    if (true == sync)
    {
        eventInfo.initLock();
    }

    {
        _HSM_SYNC_EVENTS_QUEUE();

        if (true == clearQueue)
        {
            clearPendingEvents();
        }

        mPendingEvents.push_back(eventInfo);
    }

    __TRACE_DEBUG__("transitionEx: emit");
    mDispatcher->emitEvent();

    if (true == sync)
    {
        __TRACE_DEBUG__("transitionEx: wait...");
        eventInfo.wait(timeoutMs);
        status = (HsmEventStatus_t::DONE_OK == *eventInfo.transitionStatus);
    }
    else
    {
        // always return true for async transitions
        status = true;
    }

    return status;
}

template <typename HsmStateEnum, typename HsmEventEnum>
template <typename... Args>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::transition(const HsmEventEnum event, Args... args)
{
    __TRACE_CALL_DEBUG_ARGS__("event=%d", static_cast<int>(event));

    transitionEx(event, false, false, 0, args...);
}

template <typename HsmStateEnum, typename HsmEventEnum>
template <typename... Args>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::transitionSync(const HsmEventEnum event, const int timeoutMs, Args... args)
{
    __TRACE_CALL_DEBUG_ARGS__("event=%d", static_cast<int>(event));
    return transitionEx(event, false, true, timeoutMs, args...);
}

template <typename HsmStateEnum, typename HsmEventEnum>
template <typename... Args>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::transitionWithQueueClear(const HsmEventEnum event,
                                                                                    Args... args)
{
    __TRACE_CALL_DEBUG_ARGS__("event=%d", static_cast<int>(event));

    transitionEx(event, true, false, 0, args...);
}

template <typename HsmStateEnum, typename HsmEventEnum>
template <typename... Args>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::isTransitionPossible(const HsmEventEnum event,
                                                                                Args... args)
{
    __TRACE_CALL_DEBUG_ARGS__("event=%d", static_cast<int>(event));

    HsmStateEnum stateFrom = mCurrentState;
    TransitionInfo possibleTransition;
    HsmEventEnum nextEvent;
    VariantList_t transitionArgs;
    bool possible = true;

    makeVariantList(transitionArgs, args...);

    {
        _HSM_SYNC_EVENTS_QUEUE();

        for (auto it = mPendingEvents.begin(); (it != mPendingEvents.end()) && (true == possible); ++it)
        {
            nextEvent = it->type;
            possible = findTransitionTarget(stateFrom, nextEvent, transitionArgs, possibleTransition);

            if (true == possible)
            {
                stateFrom = possibleTransition.destinationState;
            }
        }
    }

    if (true == possible)
    {
        nextEvent = event;
        possible = findTransitionTarget(stateFrom, nextEvent, transitionArgs, possibleTransition);
    }

    __TRACE_CALL_RESULT__("%d", BOOL2INT(possible));
    return possible;
}

// ============================================================================
// PRIVATE
// ============================================================================
template <typename HsmStateEnum, typename HsmEventEnum>
template <typename... Args>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::makeVariantList(VariantList_t& vList, Args&&... args)
{
    volatile int make_variant[] = {0, (vList.push_back(Variant::make(std::forward<Args>(args))), 0)...};
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::dispatchEvents()
{
    __TRACE_CALL_DEBUG_ARGS__("dispatchEvents: mPendingEvents.size()=%ld", mPendingEvents.size());

    if (false == mStopDispatching)
    {
        if (false == mPendingEvents.empty())
        {
            PendingEventInfo pendingEvent;

            {
                _HSM_SYNC_EVENTS_QUEUE();
                pendingEvent = mPendingEvents.front();

                mPendingEvents.pop_front();
            }

            HsmEventStatus_t transitiontStatus = doTransition(pendingEvent);

            __TRACE_DEBUG__("dispatchEvents: unlock with status %d", static_cast<int>(transitiontStatus));
            pendingEvent.unlock(transitiontStatus);
        }

        if ((false == mStopDispatching) && (false == mPendingEvents.empty()))
        {
            mDispatcher->emitEvent();
        }
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::onStateExiting(const HsmStateEnum state)
{
    bool res = true;
    auto it = mRegisteredStates.find(state);

    if ((mRegisteredStates.end() != it) && it->second.onExiting)
    {
        res = it->second.onExiting();
    }

    return res;
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::onStateEntering(const HsmStateEnum state,
                                                                           const VariantList_t& args)
{
    bool res = true;
    auto it = mRegisteredStates.find(state);

    if ((mRegisteredStates.end() != it) && it->second.onEntering)
    {
        res = it->second.onEntering(args);
    }

    return res;
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::onStateChanged(const HsmStateEnum state,
                                                                          const VariantList_t& args)
{
    __TRACE_CALL_DEBUG_ARGS__("onStateChanged with state <%d>", static_cast<int>(state));
    auto it = mRegisteredStates.find(state);

    if ((mRegisteredStates.end() != it) && it->second.onStateChanged)
    {
        it->second.onStateChanged(args);
    }
    else
    {
        __TRACE_DEBUG__("WARNING: no callback registered for state <%d>", static_cast<int>(state));
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::getParentState(const HsmStateEnum child,
                                                                          HsmStateEnum& outParent)
{
    bool wasFound = false;
    auto it = std::find_if(mSubstates.begin(), mSubstates.end(),
                          [child](const auto& itemIt){ return (child == itemIt.second); });

    if (mSubstates.end() != it)
    {
        outParent = it->first;
        wasFound = true;
    }

    return wasFound;
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::findTransitionTarget(const HsmEventEnum event,
                                                                                                 const VariantList_t& transitionArgs,
                                                                                                 TransitionInfo& outTransition)
{
    return findTransitionTarget(mCurrentState, event, transitionArgs, outTransition);
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::findTransitionTarget(const HsmStateEnum fromState,
                                                                                                 const HsmEventEnum event,
                                                                                                 const VariantList_t& transitionArgs,
                                                                                                 TransitionInfo& outTransition)
{
    bool wasFound = false;
    bool continueSearch;
    HsmStateEnum curState = fromState;

    do
    {
        auto key = std::make_pair(curState, event);
        auto itRange = mTransitionsByEvent.equal_range(key);

        continueSearch = false;

        if (itRange.first == itRange.second)
        {
            HsmStateEnum parentState;
            bool hasParent = getParentState(curState, parentState);

            if (true == hasParent)
            {
                curState = parentState;
                continueSearch = true;
            }
        }
        else
        {
            for (auto it = itRange.first; it != itRange.second; ++it)
            {
                if (nullptr == it->second.checkCondition)
                {
                    outTransition = it->second;
                    wasFound = true;
                    break;
                }
                else
                {
                    if (true == it->second.checkCondition(transitionArgs))
                    {
                        outTransition = it->second;
                        wasFound = true;
                        break;
                    }
                }
            }
        }
    } while (true == continueSearch);

    return wasFound;
}

template <typename HsmStateEnum, typename HsmEventEnum>
typename HsmEventStatus_t HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::doTransition(const PendingEventInfo& event)
{
    __TRACE_CALL_DEBUG_ARGS__("event=%d, entryPointTransition=%s", static_cast<int>(event.type), BOOL2STR(event.entryPointTransition));
    HsmEventStatus_t res = HsmEventStatus_t::DONE_FAILED;
    TransitionInfo transitionInfo;
    bool correctTransition = false;

    if (true == event.entryPointTransition)
    {
        if (true == getEntryPoint(mCurrentState, transitionInfo.destinationState))
        {
            correctTransition = true;
        }
    }
    else if (true == findTransitionTarget(event.type, event.args, transitionInfo))
    {
        correctTransition = true;
    }

    if (true == correctTransition)
    {
        if (mCurrentState != transitionInfo.destinationState)
        {
            // NOTE: Decide if we need functionality to cancel ongoing transition
            if (true == onStateExiting(mCurrentState))
            {
                if (transitionInfo.onTransition)
                {
                    transitionInfo.onTransition(event.args);
                }

                if (true == onStateEntering(transitionInfo.destinationState, event.args))
                {
                    mCurrentState = transitionInfo.destinationState;
                    onStateChanged(transitionInfo.destinationState, event.args);

                    HsmStateEnum entryPoint;

                    if (true == getEntryPoint(mCurrentState, entryPoint))
                    {
                        __TRACE_DEBUG__("state <%d> has substates with entry point <%d>",
                                        static_cast<int>(mCurrentState), static_cast<int>(entryPoint));
                        PendingEventInfo entryPointTransitionEvent;

                        entryPointTransitionEvent.entryPointTransition = true;
                        entryPointTransitionEvent.args = event.args;
                        entryPointTransitionEvent.cvLock = event.cvLock;
                        entryPointTransitionEvent.syncProcessed = event.syncProcessed;
                        entryPointTransitionEvent.transitionStatus = event.transitionStatus;

                        {
                            _HSM_SYNC_EVENTS_QUEUE();
                            mPendingEvents.push_front(entryPointTransitionEvent);
                        }
                        res = HsmEventStatus_t::PENDING;
                    }
                    else
                    {
                        res = HsmEventStatus_t::DONE_OK;
                    }
                }
                else
                {
                    // to prevent infinite loops we don't allow state to cancel transition
                    onStateEntering(mCurrentState, VariantList_t());
                    onStateChanged(mCurrentState, VariantList_t());
                }
            }
        }
        else if (transitionInfo.onTransition)
        {
            transitionInfo.onTransition(event.args);
            res = HsmEventStatus_t::DONE_OK;
        }
    }

    if (HsmEventStatus_t::DONE_FAILED == res)
    {
        __TRACE_DEBUG__("event <%d> in state <%d> was ignored.", static_cast<int>(event.type), static_cast<int>(mCurrentState));
    }

    return res;
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::clearPendingEvents()
{
    __TRACE_CALL_DEBUG_ARGS__("clearPendingEvents: mPendingEvents.size()=%ld", mPendingEvents.size());

    for (auto it = mPendingEvents.begin(); (it != mPendingEvents.end()) ; ++it)
    {
        // since ongoing transitions can't be canceled we need to treat entry point transitions as atomic
        if (false == it->entryPointTransition)
        {
            it->releaseLock();
        }
    }

    mPendingEvents.clear();
}

// ============================================================================
// PRIVATE: PendingEventInfo
// ============================================================================
template <typename HsmStateEnum, typename HsmEventEnum>
HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::PendingEventInfo::~PendingEventInfo()
{
    if (true == cvLock.unique())
    {
        __TRACE_CALL_DEBUG_ARGS__("event=%d was deleted. releasing lock", static_cast<int>(type));
        unlock(HsmEventStatus_t::DONE_FAILED);
        cvLock.reset();
        syncProcessed.reset();
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::PendingEventInfo::initLock()
{
    if (!cvLock)
    {
        cvLock = std::make_shared<std::mutex>();
        syncProcessed = std::make_shared<std::condition_variable>();
        transitionStatus = std::make_shared<HsmEventStatus_t>();
        *transitionStatus = HsmEventStatus_t::PENDING;
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::PendingEventInfo::releaseLock()
{
    if (isSync())
    {
        __TRACE_CALL_DEBUG_ARGS__("releaseLock");
        unlock(HsmEventStatus_t::DONE_FAILED);
        cvLock.reset();
        syncProcessed.reset();
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::PendingEventInfo::isSync()
{
    return (nullptr != cvLock);
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::PendingEventInfo::wait(const int timeoutMs)
{
    if (isSync())
    {
        std::unique_lock<std::mutex> lck(*cvLock);

        __TRACE_CALL_DEBUG_ARGS__("trying to wait... (current status=%d, %p)", static_cast<int>(*transitionStatus), transitionStatus.get());
        if (timeoutMs > 0)
        {
            syncProcessed->wait_for(lck, std::chrono::milliseconds(timeoutMs),
                                    [=](){return (HsmEventStatus_t::PENDING != *transitionStatus);});
        }
        else
        {
            syncProcessed->wait(lck, [=](){return (HsmEventStatus_t::PENDING != *transitionStatus);});
        }

        __TRACE_DEBUG__("unlocked! transitionStatus=%d", static_cast<int>(*transitionStatus));
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
void HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::PendingEventInfo::unlock(const HsmEventStatus_t status)
{
    __TRACE_CALL_DEBUG_ARGS__("try to unlock with status=%d", static_cast<int>(status));

    if (isSync())
    {
        __TRACE_DEBUG__("SYNC object (%p)", transitionStatus.get());
        *transitionStatus = status;

        if (status != HsmEventStatus_t::PENDING)
        {
            syncProcessed->notify_one();
        }
    }
    else
    {
        __TRACE_DEBUG__("ASYNC object");
    }
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::getEntryPoint(const HsmStateEnum state,
                                                                                          HsmStateEnum &outEntryPoint) const
{
    bool hasEntryPoint = false;
    auto it = mSubstateEntryPoint.find(state);

    if (it != mSubstateEntryPoint.end())
    {
        outEntryPoint = it->second;
        hasEntryPoint = true;
    }

    return hasEntryPoint;
}

#ifdef HSM_ENABLE_SAFE_STRUCTURE

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::isTopState(const HsmStateEnum state) const
{
    auto it = std::find(mTopLevelStates.begin(), mTopLevelStates.end(), state);

    return (it == mTopLevelStates.end());
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::isSubstate(const HsmStateEnum state) const
{
    bool result = false;

    for (auto itSubstate = mSubstates.begin(); itSubstate != mSubstates.end(); ++itSubstate)
    {
        if (itSubstate->second == state)
        {
            result = true;
            break;
        }
    }

    return result;
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::hasSubstates(const HsmStateEnum state) const
{
    return (mSubstates.find(state) != mSubstates.end());
}

template <typename HsmStateEnum, typename HsmEventEnum>
bool HierarchicalStateMachine<HsmStateEnum, HsmEventEnum>::hasParentState(const HsmStateEnum state,
                                                                                           HsmStateEnum &outParent) const
{
    bool hasParent = false;

    for (auto it = mSubstates.begin(); it != mSubstates.end(); ++it)
    {
        if (state == it->second)
        {
            hasParent = true;
            outParent = it->first;
            break;
        }
    }

    return hasParent;
}

#endif // HSM_ENABLE_SAFE_STRUCTURE

#endif  // __HSMCPP_HSM_HPP__