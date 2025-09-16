// 空函数指针，会被vue实例方法全局覆盖
function showError(errorCode, strInfo) {

}

function showProgress(info) {

}

function showConfirm(info) {

}

function closeView() {
    document.body.style.display = 'none';
}


function OnOkClick() {
    bridge.receiveMessageFromJS("OK");
}
function OnCancelClicked() {
    bridge.receiveMessageFromJS("Cancel");
}

function setInfo(errorCode, strInfo) {
    // 执行逻辑
    // if (errorCodeValue.value) {
    //     errorCodeValue.value = errorCode
    // } else if (confirminfo.value) {
    //     confirminfo.value = strInfo
    // } else if (progressInfo.value) {
    //     progressInfo.value = strInfo
    // }
}

document.addEventListener("DOMContentLoaded", function () {
    new QWebChannel(qt.webChannelTransport, function (channel) {
        window.bridge = channel.objects.bridge;
    });
});

// test
// setTimeout(() => {
//     showProgress('Host is not reachable or there is no network connection.',400,'自定义标题');
// }, 1000)

// setTimeout(() => {
//     showError('404', 'sadfgesdfg');
//     // setTimeout(() => {
//     //     setInfo('info','12345');
//     // }, 1000)
// }, 1000)
// setTimeout(() => {
//     showConfirm('404', 'Are you sure you want to proceed??');
//     // setTimeout(() => {
//     //     setInfo('info','12345');
//     // }, 1000)
// }, 1000)