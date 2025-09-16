#pragma once
// include this header file if you want to show a message
#include <unordered_map>
#include <QNetworkReply>

enum class MsgType
{
    Information,
    Progress,
    Confirm
};

enum class MsgCode
{
    Query_Wordbook_Timeout = -99,
    Run_Session_Timeout =-100,
    Run_Session_Outofrange =-101,
    Run_Session_Unknown =-102,
    NO_CODE = 0,
    //Network issue
    Network_OperationCanceledError = QNetworkReply::OperationCanceledError,
    SslHandshakeFailedError = QNetworkReply::SslHandshakeFailedError,
    Network_Time_Out,
    No_Connection,
    Client_Network_Error,

    Host_Get_Session_Key_Failed = 462,
    //Host side code
    Host_Not_Ready = 500,
    Host_Busy = 503,
    Another_User_Connected = 600,
    Host_Is_Initing = 601,

    // Normal Msg
    SHOW_INFO_VIEW,
    REMDER_HAVE_FIRSTFRAME,
    StartPair,
    StartConnect,
    QUIT,
    CloseInfo
};

const std::unordered_map<MsgCode, std::string> MsgMap =
{
    {MsgCode::Query_Wordbook_Timeout,              "Abnormal host status, try restart stream service"},
    {MsgCode::Run_Session_Timeout,                 "Abnormal host status, try restart stream service"},
    {MsgCode::Run_Session_Outofrange,              "Abnormal host status, try restart stream service"},
    {MsgCode::Run_Session_Unknown,                 "Abnormal host status, try restart stream service"},
    {MsgCode::NO_CODE,                             "No error code"},
    {MsgCode::REMDER_HAVE_FIRSTFRAME,              "REMDER_HAVE_FIRSTFRAME"},
    {MsgCode::SslHandshakeFailedError,             "Host ssl hand shake failed."},
    {MsgCode::Network_OperationCanceledError,      "Host is not reachable or there is no network connection."},
    {MsgCode::Network_Time_Out,                    "Network_Time_Out"},
    {MsgCode::No_Connection,                       "Host is not reachable or there is no network connection."},
    {MsgCode::Client_Network_Error,                "Local network error, please check."},
    {MsgCode::Host_Get_Session_Key_Failed,         "Host get session key failed."},
    {MsgCode::Host_Not_Ready,                      "Host side is not ready"},
    {MsgCode::Host_Busy,                           "Host side is busy"},
    {MsgCode::SHOW_INFO_VIEW,                      "SHOW_INFO_VIEW"},
    {MsgCode::StartConnect,                        "Connecting ..."},
    {MsgCode::QUIT,                                "Quit application?"},
    {MsgCode::CloseInfo,                           "CloseInfo"},
    {MsgCode::Another_User_Connected,              "Another user has connected to the server."},
    {MsgCode::Host_Is_Initing,                     "Host side is initializing, please wait."},
};

#include "uiStateMachine.h"
