#pragma once

#include <iomanip>
#include <map>
#include <functional>

#include "ccronexpr/ccronexpr.h"


#include <algorithm>
#include <unordered_map>
#include <ctime>
#include <math.h>
#include <memory>

enum WFTimerEnum
{
    MAX_SLOT = 10000,               //时间轮槽位数量
    SLOT_TIME = 20,                 //每个槽位时间长度 ms
    WHEEL_LEVEL = 2,
    //WHEEL_TIME = MAX_SLOT * SLOT_TIME,  //一个时间轮时间长度 ms
};

//enum TimerDataState //定时器 状态
//{
//    TimerData_AddData = 0,        //定时器数据对象添加
//    TimerData_AddSlot,        //定时器已经加入时间轮
//    TimerData_BeRemoved,        //定时器已经被删除状态
//    TimerData_Pause,        //定时器暂停
//    TimerData_Running,      //正在运行
//};

enum TimerLevel
{
    OUT_WHEEL_LEVEL = WHEEL_LEVEL + 1,
    PAUSE_TIMER_LEVEL,
    RUNNING_SLOT,
    //ADD_DATA,
    REMOVE,
};


using Clock = std::chrono::system_clock;

class Task
{
public:
    explicit Task()
    {
    };
    virtual ~Task()
    {
    };

    virtual int32_t GetIntervalTime(uint64_t nNow) = 0;

    void BeforeExcute()
    {
        ++trigger_count;
    }
    virtual void ExcuteCallback() = 0;

    void Excute()
    {
        //if (!CanExcute())
        //{
        //    return;
        //}
        BeforeExcute();
        ExcuteCallback();
    }

    bool CanExcute()
    {
        if (level == REMOVE
                || level == PAUSE_TIMER_LEVEL)
        {
            return false;
        }
        return true;
    }

    //void SetState(TimerDataState _state)
    //{
    //    state = _state;
    //}
    //bool IsInState(TimerDataState _state)
    //{
    //    return state == _state;
    //}
    void SetLevel(int _level)
    {
        level = _level;
    }
    bool IsInLevel(int _level)
    {
        return level == _level;
    }

    bool IsTriggerLimit()
    {
        return count > 0 && count <= trigger_count;
    }

    int type = 0;
    //TimerDataState state = TimerData_AddData;     //TimerDataState
    int32_t count = 0;                     //可触发次数 <0 无限次
    uint32_t trigger_count = 0;            //已经触发次数
    uint32_t slot = 0;                      //生效槽位索引
    uint32_t level = 0;
    uint32_t left_ticks = 0;                //
    WFGUID entity_id = { 0, 0 };

    Task* pPrev = nullptr;
    Task* pNext = nullptr;
};

struct TimeWheel
{
    TimeWheel(uint32_t level)
        : now_slot(0), slot_vec(MAX_SLOT, nullptr)
    {
        slot_ticks = std::pow((int)MAX_SLOT, level);
        wheel_ticks = slot_ticks * MAX_SLOT;
        //slot_vec.resize(MAX_SLOT);
    }
    uint32_t slot_ticks;        //当前level 每经过一个槽位需要消耗的 tick 数量，第0 层  = 1
    uint32_t wheel_ticks;       //当前level 经过一轮需要消耗的 tick 数量
    uint32_t now_slot; //当前槽位
    //std::vector<std::list<Task*>> slot_vec;
    std::vector<Task*> slot_vec;
};

//class InTask : public Task
//{
//public:
//    explicit InTask(std::function<int(uint32_t)>&& f) : Task(std::move(f)) {}
//
//    // dummy time_point because it's not used
//    Clock::time_point get_new_time() const override
//    {
//        return Clock::time_point(Clock::duration(0));
//    }
//};

class EveryTask : public Task
{
public:
    EveryTask(int32_t delay_time, uint32_t interval_time, TimerCB&& f, const AFIDataList& arg_list) :
        callback(std::move(f)), delay_time(delay_time), interval_time(interval_time), arg_list(arg_list) {}

    virtual int32_t GetIntervalTime(uint64_t nNow) override
    {
        if (delay_time < 0)
        {
            return interval_time;
        }
        else if (delay_time == 0)
        {
            delay_time = -1;
            return 0;
        }
        else    // (delay_time > 0)
        {
            uint32_t int_t = delay_time;
            delay_time = -1;
            return int_t;
        }
    };
    virtual void ExcuteCallback() override
    {
        callback(entity_id, trigger_count, arg_list);
    };
    int32_t delay_time;
    uint64_t interval_time;
    TimerCB callback;       //定时器触发回调
    AFCDataList arg_list;

};

class ComplicatedCronTask : public Task
{
public:
    ComplicatedCronTask(const std::string& expression, uint32_t delay_time, CronCB&& f) :
        callback(std::move(f)), delay_time(delay_time)
    {
        memset(&cron_parser, 0, sizeof(cron_expr));
        const char* err_msg = nullptr;
        cron_parse_expr(expression.c_str(), &cron_parser, &err_msg);
        if (err_msg != nullptr)
        {
            throw Bosma::BadCronExpression("malformed cron string: " + expression);
        }
    }

    virtual int32_t GetIntervalTime(uint64_t server_now) override
    {
        //server_now -= WFTime::GetTimeZoneMillisecond();
        time_t next_time = cron_next(&cron_parser, (server_now + delay_time) / WFTime::NSECOND_MS) * WFTime::NSECOND_MS;
        if (delay_time > 0)
        {
            delay_time = 0;
        }
        return next_time - server_now;
    };
    virtual void ExcuteCallback() override
    {
        callback();
    };
    uint32_t delay_time;
    cron_expr cron_parser;
    CronCB callback;       //定时器触发回调
};

class Scheduler
{
public:
    explicit Scheduler()
        : m_nLastUpdateTime(0), m_nInsertTime(0)
    {
        m_vecTimers.resize(WHEEL_LEVEL);
        for (int i = 0; i < WHEEL_LEVEL; ++i)
        {
            m_vecTimers[i] = std::make_shared<TimeWheel>(i);
        }
    }

    Scheduler(const Scheduler&) = delete;

    Scheduler(Scheduler&&) noexcept = delete;

    Scheduler& operator=(const Scheduler&) = delete;

    Scheduler& operator=(Scheduler&&) noexcept = delete;

    virtual ~Scheduler()
    {
        for (auto& iter : m_mapTimers)
        {
            for (auto& it : iter.second)
            {
                delete it.second;
            }
        }
        UpdateTimerDelete();
    }

    void Init(uint64_t nNow)
    {
        m_nLastUpdateTime = nNow;
        m_nInsertTime = m_nLastUpdateTime;
    }

    void Update(uint64_t nNow)
    {
        //UpdateTimerReg();
        UpdateTimer(nNow);
        UpdateTimerDelete();
    }

    void Shut()
    {
        for (auto& iter : m_mapTimers)
        {
            for (auto& it : iter.second)
            {
                delete it.second;
            }
        }
        for (int i = 0; i < WHEEL_LEVEL; ++i)
        {
            m_vecTimers[i]->slot_vec.clear();
        }
        m_mapTimers.clear();

        m_pOutWheelTimers = nullptr;

        //m_listRegTimers.clear();
        UpdateTimerDelete();
    }

    bool AddTimer(const EScheduleType eType, const WFGUID& xEntityID, int32_t nDelayTime, uint32_t nIntervalTime, int32_t nCount, TimerCB& funCallback, const WFIDataList& xArgList)
    {
        Task* pData = new EveryTask(nDelayTime, nIntervalTime, std::move(funCallback), xArgList); // (WFTimerData*)ARK_ALLOC(sizeof(WFTimerData));
        //memset(data, 0, sizeof(WFTimerData));
        //memcpy(data->name, strName.c_str(), (strName.length() > 16) ? 16 : strName.length());
        pData->type = eType;
        pData->count = (0 == nCount) ? -1 : nCount;
        pData->entity_id = xEntityID;

        if (!AddTimerData(pData->entity_id, eType, pData))
        {
            delete pData;
            return false;
        }
        return true;
    }

    bool AddCronAfterStartTime(const EScheduleType eType, time_t nNow, const std::string& strStartTime, const std::string& strExpression, CronCB& funCallback)
    {
        time_t nFirstAddTime = 0;
        if (!strStartTime.empty())
        {
            nFirstAddTime = WFTime::GetTimeFromYMDHMSMString(strStartTime);
            //first_add_time = GetTimeFromTimerString(start_time, now);
            if (nFirstAddTime == 0)
            {
                return false;
            }
            nFirstAddTime -= nNow;
            if (nFirstAddTime < 0)
            {
                nFirstAddTime = 0;
            }
        }
        return AddComplicatedCron(eType, nFirstAddTime/* * WFTime::NSECOND_MS*/, strExpression, funCallback);
        //return AddCron(type, first_add_time * WFTime::NSECOND_MS, expression, callback);
    }
    bool AddCron(const EScheduleType eType, const uint32_t nDelayTime, const std::string& strExpression, CronCB& funCallback)
    {
        try
        {
            Task* pData = new CronTask(strExpression, nDelayTime, std::move(funCallback)); // (WFTimerData*)ARK_ALLOC(sizeof(WFTimerData));
            //memset(data, 0, sizeof(WFTimerData));
            //memcpy(data->name, strName.c_str(), (strName.length() > 16) ? 16 : strName.length());
            pData->type = eType;
            pData->count = -1;

            if (!AddTimerData(pData->entity_id, eType, pData))
            {
                delete pData;
                return false;
            }
        }
        catch (Bosma::BadCronExpression& e)
        {
            return false;
        }
        return true;
    }

    bool AddComplicatedCron(const EScheduleType eType, const uint32_t nDelayTime, const std::string& strExpression, CronCB& funCallback)
    {
        try
        {
            Task* pData = new ComplicatedCronTask(strExpression, nDelayTime, std::move(funCallback)); // (WFTimerData*)ARK_ALLOC(sizeof(WFTimerData));
            //memset(data, 0, sizeof(WFTimerData));
            //memcpy(data->name, strName.c_str(), (strName.length() > 16) ? 16 : strName.length());
            pData->type = eType;
            pData->count = -1;

            if (!AddTimerData(pData->entity_id, eType, pData))
            {
                delete pData;
                return false;
            }
            //AddSlotTimer(data, false);
        }
        catch (Bosma::BadCronExpression& e)
        {
            return false;
        }
        return true;
    }

    bool FindTimer(const EScheduleType eType, const WFGUID& xEntityID, bool* pIsPause)
    {
        auto pTask = GetTask(eType, xEntityID);
        if (pTask == nullptr)
        {
            return false;
        }
        if (pIsPause)
        {
            *pIsPause = pTask->IsInLevel(PAUSE_TIMER_LEVEL);
        }
        return true;
    }

    void RemoveTimer(const WFGUID& xEntityID)
    {
        RemoveTimerData(xEntityID);
    }

    void RemoveTimer(const EScheduleType eType, const WFGUID& xEntityID)
    {
        RemoveTimerData(eType, xEntityID);
    }

    uint64_t GetLastUpdateTime()
    {
        return m_nLastUpdateTime;
    }

    bool PauseTimer(const EScheduleType eType, const WFGUID& xEntityID)
    {
        Task* data = GetTask(eType, xEntityID);
        if (data == nullptr)
        {
            return false;
        }
        //待删除定时器
        if (data->IsTriggerLimit())
        {
            return false;
        }
        if (data->IsInLevel(PAUSE_TIMER_LEVEL))
        {
            return true;
        }
        int left_ticks = GetLeftTicks(data);
        if (left_ticks < 0)
        {
            return false;
        }
        data->left_ticks = left_ticks;

        if (!data->IsInLevel(RUNNING_SLOT))
        {
            PauseTimerData(data);
        }
        data->SetLevel(PAUSE_TIMER_LEVEL);
        return true;
    }
    bool ResumeTimer(const EScheduleType eType, const WFGUID& xEntityID)
    {
        Task* data = GetTask(eType, xEntityID);
        if (data == nullptr)
        {
            return false;
        }
        if (!data->IsInLevel(PAUSE_TIMER_LEVEL))
        {
            return false;
        }
        AddSlotTimerByLeftTicks(data);
        return true;
    }
    //
    int GetTimerLeftTime(const EScheduleType eType, const WFGUID& xEntityID)
    {
        Task* data = GetTask(eType, xEntityID);
        if (data == nullptr)
        {
            return -1;
        }
        return GetLeftTicks(data) * SLOT_TIME;
    }
private:

    Task* GetTask(const EScheduleType eType, const WFGUID& xEntityID)
    {
        auto iter = m_mapTimers.find(xEntityID);
        if (iter == m_mapTimers.end())
        {
            return nullptr;
        }
        auto it = iter->second.find(eType);
        if (it == iter->second.end())
        {
            return nullptr;
        }

        return it->second;
    }

    int GetLeftTicks(Task* data)
    {
        if (data == nullptr)
        {
            return -1;
        }

        switch (data->level)
        {
        case PAUSE_TIMER_LEVEL:
            return data->left_ticks;
        case RUNNING_SLOT:
            {
                if (data == m_pCurTimer)
                {
                    if (data->IsTriggerLimit())
                    {
                        return -1;
                    }
                    return data->GetIntervalTime(m_nLastUpdateTime) / SLOT_TIME;
                }
                return 0;
            }
        case OUT_WHEEL_LEVEL:
            {
                int nLeftTicks = data->left_ticks;
                for (size_t i = 0; i < WHEEL_LEVEL; i++)
                {
                    nLeftTicks += GetWheelLeftTicks(i);
                }
                return nLeftTicks;
            }
        //case ADD_DATA:
        //    return data->GetIntervalTime(m_nLastUpdateTime) / SLOT_TIME;
        default:
            if (data->level < WHEEL_LEVEL)
            {
                auto pWheelData = m_vecTimers[data->level];
                int nLeftWheelSlot = 0;
                if (pWheelData->now_slot > data->slot)
                {
                    nLeftWheelSlot = data->slot + MAX_SLOT - pWheelData->now_slot;
                }
                else
                {
                    nLeftWheelSlot = data->slot - pWheelData->now_slot;
                }
                int nLeftTicks = nLeftWheelSlot * pWheelData->slot_ticks + data->left_ticks;
                for (size_t i = 0; i < data->level; i++)
                {
                    nLeftTicks += GetWheelLeftTicks(i);
                }
                return nLeftTicks;
            }
            break;
        }
        return -1;
    }
    int GetWheelLeftTicks(int nLevel)
    {
        if (nLevel >= WHEEL_LEVEL)
        {
            return 0;
        }
        auto pWheelData = m_vecTimers[nLevel];
        if (pWheelData->now_slot >= MAX_SLOT)
        {
            return 0;
        }

        return (MAX_SLOT - pWheelData->now_slot) * pWheelData->slot_ticks;
    }

    //void UpdateTimerReg()
    //{
    //    for (auto data : m_listRegTimers)
    //    {
    //        if (data->IsInLevel(PAUSE_TIMER_LEVEL))
    //        {
    //            continue;
    //        }
    //        if (data->IsInLevel(REMOVE))
    //        {
    //            delete data;
    //            continue;
    //        }
    //        if (!AddSlotTimer(data))
    //        {
    //            WFLOG_ERROR(NULL_GUID, "timer add error,", data->type, ",", data->GetIntervalTime(m_nLastUpdateTime));
    //            delete data;
    //        }
    //    }
    //    m_listRegTimers.clear();
    //}
    void UpdateTimerDelete()
    {
        for (auto data : m_listRemoveTimers)
        {
            delete data;
        }
        m_listRemoveTimers.clear();
    }

    void UpdateTimer(uint64_t nNowTime)
    {
        if (nNowTime < m_nLastUpdateTime)
        {
            m_nLastUpdateTime = nNowTime;
            m_nInsertTime = m_nLastUpdateTime;
            return;
        }
        uint64_t nPassedSlot = (nNowTime - m_nLastUpdateTime) / SLOT_TIME;
        if (nPassedSlot == 0)
        {
            return;
        }
        auto pWheelData = m_vecTimers[0];
        m_nInsertTime = nPassedSlot * SLOT_TIME + m_nLastUpdateTime;
        for (uint64_t i = 0; i < nPassedSlot; ++i)
        {
            m_nLastUpdateTime += SLOT_TIME;
            //m_nInsertTime -= SLOT_TIME;
            //
            //++pWheelData->now_slot;
            UpdateSlotTimer(pWheelData->now_slot++);

            if (pWheelData->now_slot >= MAX_SLOT)
            {
                pWheelData->now_slot = 0;
                CascadeTimer(0);
            }
        }
    }

    void UpdateSlotTimer(uint32_t nSlot)
    {
        auto& pWheelData = m_vecTimers[0];
        Task* data = pWheelData->slot_vec[nSlot];
        pWheelData->slot_vec[nSlot] = nullptr;
        //标记一下当前轮次的定时器，
        //当前运行定时器有可能重新添加到下一轮的当前轮次中
        //需要标记区分一下，
        for (Task* r_task = data; r_task != nullptr; r_task = r_task->pNext)
        {
            r_task->SetLevel(RUNNING_SLOT);
        }

        for (; data != nullptr;)
        {
            m_pCurTimer = data;
            data = data->pNext;

            if (!m_pCurTimer->CanExcute())
            {
                continue;
            }

            m_pCurTimer->Excute();
            //如果在 定时器的回调中 删除自己 或者暂停自己
            if (!m_pCurTimer->CanExcute())
            {
                continue;
            }
            //触发次数已达上限
            if (m_pCurTimer->IsTriggerLimit())
            {
                RemoveTimerData(m_pCurTimer->type, m_pCurTimer->entity_id);
                continue;
            }
            if (!AddSlotTimer(m_pCurTimer))
            {
                WFLOG_ERROR(NULL_GUID, "timer update add error,", m_pCurTimer->type, ",", m_pCurTimer->GetIntervalTime(m_nLastUpdateTime));
                delete m_pCurTimer;
            }
            //AddSlotTimer(m_pCurTimer);
        }
        m_pCurTimer = nullptr;
    }
    bool AddTimerData(const WFGUID& xEntityID, const EScheduleType eType, Task* pTimerData)
    {
        auto iter = m_mapTimers.find(xEntityID);

        if (iter == m_mapTimers.end())
        {
            std::unordered_map<int, Task*> tmp;
            iter = m_mapTimers.insert(std::make_pair(xEntityID, tmp)).first;
        }
        //pTimerData->SetLevel(ADD_DATA);
        if (!iter->second.insert(std::make_pair(eType, pTimerData)).second)
        {
            return false;
        }
        //有些逻辑会依赖延迟添加机制，所以先保留下一帧再添加
        if (!AddSlotTimer(pTimerData))
        {
            return false;
        }
        //m_listRegTimers.push_back(pTimerData);
        return true;
    }

    void RemoveTimerData(const WFGUID& xEntityID)
    {
        auto iter = m_mapTimers.find(xEntityID);

        if (iter == m_mapTimers.end())
        {
            return;
        }

        for (auto it : iter->second)
        {
            DeleteTimerData(it.second);
        }

        iter->second.clear();
        m_mapTimers.erase(iter);
        return;
    }

    void RemoveTimerData(const int nType, const WFGUID& xEntityID)
    {
        auto iter = m_mapTimers.find(xEntityID);
        if (iter == m_mapTimers.end())
        {
            return;
        }
        auto it = iter->second.find(nType);
        if (it == iter->second.end())
        {
            return;
        }
        Task* data = it->second;

        iter->second.erase(it);
        if (iter->second.empty())
        {
            m_mapTimers.erase(iter);
        }

        DeleteTimerData(data);
        return;
    }

    bool AddSlotTimer(Task* pTimerData)
    {
        //if (pTimerData == nullptr)
        //{
        //    return;
        //}
        int32_t nIntTime = pTimerData->GetIntervalTime(m_nInsertTime);
        if (nIntTime < 0)
        {
            return false;
        }
        pTimerData->left_ticks = (nIntTime + m_nInsertTime - m_nLastUpdateTime) / SLOT_TIME;
        AddSlotTimerByLeftTicks(pTimerData);
        return true;
    }

    void AddSlotTimerByLeftTicks(Task* pTimerData)
    {
        //if (pTimerData == nullptr)
        //{
        //    return;
        //}
        for (uint32_t i = 0; i < WHEEL_LEVEL; ++i)
        {
            auto& pWheelData = m_vecTimers[i];

            if (pTimerData->left_ticks >= pWheelData->wheel_ticks)
            {
                pTimerData->left_ticks -= pWheelData->slot_ticks * (MAX_SLOT - pWheelData->now_slot);
                continue;
            }

            pTimerData->slot = ((pTimerData->left_ticks / pWheelData->slot_ticks) + pWheelData->now_slot) % MAX_SLOT;
            pTimerData->left_ticks = pTimerData->left_ticks % pWheelData->slot_ticks;
            pTimerData->level = i;

            TaskNodePushFront(&(pWheelData->slot_vec[pTimerData->slot]), pTimerData);
            //pWheelData->slot_vec[pTimerData->slot].emplace_back(pTimerData);
            return;
        }
        pTimerData->level = OUT_WHEEL_LEVEL;
        TaskNodePushFront(&m_pOutWheelTimers, pTimerData);
        //m_listOutWheelTimers.emplace_back(pTimerData);
        return;
    }
    void CascadeOutWheelTimer()
    {
        auto data = m_pOutWheelTimers;
        m_pOutWheelTimers = nullptr;

        auto& pWheelData = m_vecTimers[WHEEL_LEVEL - 1];
        for (; data != nullptr;)
        {
            auto pTimerData = data;
            data = data->pNext;

            if (pTimerData->left_ticks >= pWheelData->wheel_ticks)
            {
                pTimerData->left_ticks -= pWheelData->wheel_ticks;
                pTimerData->level = OUT_WHEEL_LEVEL;

                TaskNodePushFront(&m_pOutWheelTimers, pTimerData);
                continue;
            }

            pTimerData->slot = ((pTimerData->left_ticks / pWheelData->slot_ticks) + pWheelData->now_slot) % MAX_SLOT;
            pTimerData->left_ticks = pTimerData->left_ticks % pWheelData->slot_ticks;
            pTimerData->level = WHEEL_LEVEL - 1;
            TaskNodePushFront(&(pWheelData->slot_vec[pTimerData->slot]), pTimerData);
        }
    }

    void CascadeTimer(int nWheelLevel)
    {
        int nUpperLevel = nWheelLevel + 1;
        if (nUpperLevel >= WHEEL_LEVEL)
        {
            CascadeOutWheelTimer();
            return;
        }
        auto pUpperWheelData = m_vecTimers[nUpperLevel];
        auto data = pUpperWheelData->slot_vec[pUpperWheelData->now_slot];
        pUpperWheelData->slot_vec[pUpperWheelData->now_slot] = nullptr;

        auto& pWheelData = m_vecTimers[nWheelLevel];
        for (; data != nullptr;)
        {
            auto pTimerData = data;
            data = data->pNext;

            pTimerData->slot = ((pTimerData->left_ticks / pWheelData->slot_ticks) + pWheelData->now_slot) % MAX_SLOT;
            pTimerData->left_ticks = pTimerData->left_ticks % pWheelData->slot_ticks;
            pTimerData->level = nWheelLevel;
            TaskNodePushFront(&(pWheelData->slot_vec[pTimerData->slot]), pTimerData);
        }
        ++pUpperWheelData->now_slot;
        if (pUpperWheelData->now_slot >= MAX_SLOT)
        {
            pUpperWheelData->now_slot = 0;
            CascadeTimer(nUpperLevel);
        }
    }

    //tasknode
    bool TaskNodePushFront(Task** pHeadNode, Task* pInsertNode)
    {
        if (pHeadNode == nullptr || pInsertNode == nullptr)
        {
            return false;
        }
        //pInsertNode->state = TimerData_AddSlot;
        pInsertNode->pPrev = nullptr;
        if (*pHeadNode == nullptr)
        {
            pInsertNode->pNext = nullptr;
        }
        else
        {
            (*pHeadNode)->pPrev = pInsertNode;
            pInsertNode->pNext = (*pHeadNode);
        }
        *pHeadNode = pInsertNode;
        return true;
    }

    bool TaskNodeRemove(Task** pHeadNode, Task* pRemoveNode)
    {
        if (pHeadNode == nullptr || pRemoveNode == nullptr)
        {
            return false;
        }
        //pRemoveNode->state = TimerData_BeRemoved;
        if (*pHeadNode == pRemoveNode)
        {
            *pHeadNode = pRemoveNode->pNext;
        }
        if (pRemoveNode->pNext)
        {
            pRemoveNode->pNext->pPrev = pRemoveNode->pPrev;
        }
        if (pRemoveNode->pPrev)
        {
            pRemoveNode->pPrev->pNext = pRemoveNode->pNext;
        }
        pRemoveNode->pNext = nullptr;
        pRemoveNode->pPrev = nullptr;
        return true;
    }

    void DeleteTimerData(Task* data)
    {
        if (!data)
        {
            return;
        }
        switch (data->level)
        {
        case OUT_WHEEL_LEVEL:
            TaskNodeRemove(&m_pOutWheelTimers, data);
            break;
        //case ADD_DATA:
        //    //直接返回,后续添加的时候再删除
        //    data->SetLevel(REMOVE);
        //    return;
        case RUNNING_SLOT:
            {
                data->SetLevel(REMOVE);
                m_listRemoveTimers.push_back(data);
            }
            return;
        default:
            if (data->level < WHEEL_LEVEL)
            {
                auto& head = m_vecTimers[data->level]->slot_vec[data->slot];
                TaskNodeRemove(&head, data);
            }
            break;
        }
        delete data;
    }

    void PauseTimerData(Task* data)
    {
        if (!data)
        {
            return;
        }

        if (data->level < WHEEL_LEVEL)
        {
            auto& head = m_vecTimers[data->level]->slot_vec[data->slot];
            TaskNodeRemove(&head, data);
        }
        else if (data->level == OUT_WHEEL_LEVEL)
        {
            TaskNodeRemove(&m_pOutWheelTimers, data);
        }
        //else if (data->level == ADD_DATA)
        //{
        //    m_listRegTimers.remove_if([&data](const Task * pRegTimer)
        //    {
        //        if (pRegTimer == data)
        //        {
        //            return true;
        //        }
        //        return false;
        //    });
        //}
        //data->level = PAUSE_TIMER_LEVEL;
    }


private:
    uint64_t m_nLastUpdateTime;
    uint64_t m_nInsertTime;
    std::unordered_map<WFGUID, std::unordered_map<int, Task*>> m_mapTimers;

    //std::list<Task*> m_listRegTimers;
    std::list<Task*> m_listRemoveTimers;

    Task* m_pCurTimer = nullptr;
    Task* m_pOutWheelTimers = nullptr;
    std::vector<std::shared_ptr<TimeWheel>> m_vecTimers;
    //std::list<Task*> m_listOutWheelTimers;
};
