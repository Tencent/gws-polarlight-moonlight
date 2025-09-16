var ghelpbridge;
function isMacOS() {
  return /Mac|iPod|iPhone|iPad/.test(navigator.userAgent);
}

var KeyBoardList = [
  // {
  //   key: "F4",
  //   title: "Send Alt + F4",
  //   hideOption: true,
  //   desc: "",
  // },
  {
    key: "Q",
    title: "Quit GWS Client",
  },
  {
    key: "O",
    title: "Open Settings",
    desc: "",
  },

  {
    key: "M",
    title: "Toggle mouse mode",
    desc: "Pointer capture or direct control",
  },
  {
    key: "I",
    title: "Open Display Settings panel",
  },
  {
    key: "S",
    title: "Toggle performance stats overlay",
  },

  // {
  //   key: "C",
  //   title: "Toggle local cursor display in remote desktop mouse mode",
  //   desc: "Remote cursor will always show up due to GameStream limitations"
  // },

  // {
  //   key: "X",
  //   title: "Toggle between Fullscreen and Windowed mode",
  //   desc: "",
  // },
  {
    key: "1",
    title: "Toggle client-1 between Fullscreen and Windowed mode",
  },
  {
    key: "2",
    title: "Toggle client-2 between Fullscreen and Windowed mode",
  },
  {
    key: "V",
    title: "Paste content from GWS client clipboard to GWS host",
    desc: "Pasting content from GWS host clipboard to GWS client is not supported",
  },
  {
    key: "Z",
    title:
      "Release mouse and keyboard captured by GWS host for local interaction",
    desc: "",
  },
];

new QWebChannel(qt.webChannelTransport, function (channel) {
  // Access the C++ API
  ghelpbridge = channel.objects.ghelpbridge;
  // Call a function on the C++ API
});

// 关闭窗口
function CloseWindow() {
  ghelpbridge.Close();
}
