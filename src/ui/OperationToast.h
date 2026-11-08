#pragma once

#include <QString>

namespace OperationToast {

void imageCopied();
void imageSaved();

// 居中提示（不受 general/show_operation_toast 开关影响）。
// 用于"功能未实现/敬请期待"这类必须让用户看到的一次性说明。
void centerInfo(const QString &text, int displayMsec = 2600);

} // namespace OperationToast
