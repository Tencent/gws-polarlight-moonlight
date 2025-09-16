var gleam;
var gCB = null;
var gfresh;
var SNAPSHORT_INTERVAL_TIME = 2000;
var SNAPSHORT_EMPTY_INTERVAL_TIME = 100;

new QWebChannel(qt.webChannelTransport, function (channel) {
  // Access the C++ API
  gleam = channel.objects.gleamPost;
  gfresh = channel.objects.grefresh;
  // Call a function on the C++ API
});

// Vue 方法覆盖
function SetVueSnapShot(img, requerstId) {
  console.log(img, requerstId);
}

async function initSnapShot(imageId, requestId) {
  const imgBase64 = await gleam.QueryData(imageId);
  SetVueSnapShot(imgBase64, requestId);
}

async function CallSnapShotJs(JSONObj, requestId) {
  if (JSONObj != null) {
    const result = typeof JSONObj === "string" ? JSON.parse(JSONObj) : JSONObj;
    initSnapShot(result.image, requestId);
  }
  return "0";
}

// Vue 方法覆盖
function SetVueStatus() {}
function CallStatusJs(JSONObj) {
  if (JSONObj != null && JSONObj) {
    SetVueStatus(formatStatusData(JSONObj));
  }
}

function CallJs(JSONObj) {
  if (gCB != null) {
    gCB(JSONObj);
  }
  return "0";
}

function SendConfig(config, index = 1) {
  gCB = function (result) {};
  gleam.sendMsg("config", JSON.stringify(config), index);
}

function logMessage(msg) {
  // gleam.log(JSON.stringify(msg));
}

function Setinfo() {
  window.location.href = window.location.href;
}

function QueryWordBook(req, cb, index) {
  gCB = cb;
  gleam.sendMsg("wordbook", JSON.stringify(req), index);
}

function RefreshFun(type) {
  try {
    console.log(type);
    gfresh.RefreshFun(type);
  } catch (err) {
    console.error(err);
  }
}

function formatStatusData(data) {
  let { HostDisplayList = [], displayChosen = [] } = data;
  let mainName = "";
  HostDisplayList = HostDisplayList.map((v) => {
    const isMain = v.X == 0 && v.Y == 0;
    if (isMain) {
      mainName = v.Name;
    }
    return {
      ...v,
      isMain,
    };
  });
  // displayChosen没返回的时候，默认选中主屏
  displayChosen =
    displayChosen.length > 0
      ? displayChosen.map((v) => (typeof v === "string" ? v : v.Name)).join(",")
      : mainName;
  return { ...data, HostDisplayList, displayChosen };
}

/**
 * Snapshot
 * */
async function GetSnapshotImageBase64(item, isSmallImg = true, id = -1) {
  gleam.sendMsg(
    "wordbook",
    JSON.stringify({
      Type: "SnapShot",
      Name: item.Name,
      Ratio: 50,
      Scale: isSmallImg ? 10 : 30,
    }),
    id
  );
}

// 创建VirtualDisplay
function CreateVirtualDisplay(VirtualDisplay) {
  SendConfig(
    {
      ActionType: "CreateVirtualDisplay",
      VirtualDisplay,
    },
    -1
  );
}

function SetPrivacyMode(PrivacyMode) {
  SendConfig(
    {
      ActionType: "PrivacyModeChange",
      PrivacyMode: PrivacyMode ? "Enabled" : "Disabled",
    },
    -3
  );
}

function SetPrimaryDisplay(name) {
  SendConfig(
    {
      ActionType: "SetPrimaryDisplay",
      Name: name,
    },
    -4
  );
}

function DisableDisplay(name) {
  SendConfig(
    {
      ActionType: "DisableDisplay",
      Name: name,
    },
    -4
  );
}

function StreamingApply({ PrivacyMode, DisplayChoose, VirtualDisplay }) {
  SendConfig(
    {
      ActionType: "ConfigChange",
      PrivacyMode: PrivacyMode ? "Enabled" : "Disabled",
      DisplayChoose,
      VirtualDisplay,
      //TODO 之前参数  确认是否需要
      Channels: "channel1",
      Profile: "profile1",
      Quality: "high",
      Encoder: "H.264",
      RateControl: "",
      VideoBitrate: "10",
    },
    -2
  );
}

function CloseGleam() {
  gleam.Close();
}
