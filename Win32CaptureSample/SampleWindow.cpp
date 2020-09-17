#include "pch.h"
#include "App.h"
#include "SampleWindow.h"
#include "WindowList.h"
#include "MonitorList.h"
#include "ControlsHelper.h"

namespace winrt
{
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::System;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
    using namespace Windows::UI::Composition::Desktop;
    using namespace Windows::Graphics::DirectX;
}

const std::wstring SampleWindow::ClassName = L"Win32CaptureSample";

void SampleWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEX wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = ClassName.c_str();
    wcex.hIconSm = LoadIconW(wcex.hInstance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

SampleWindow::SampleWindow(HINSTANCE instance, int cmdShow, std::shared_ptr<App> app)
{
    WINRT_ASSERT(!m_window);
    WINRT_VERIFY(CreateWindowW(ClassName.c_str(), L"Win32CaptureSample", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, cmdShow);
    UpdateWindow(m_window);

    auto isAllDisplaysPresent = winrt::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 9);

    m_app = app;
    m_windows = std::make_unique<WindowList>();
    m_monitors = std::make_unique<MonitorList>(isAllDisplaysPresent);
    m_pixelFormats = 
    {
        { L"B8G8R8A8UIntNormalized", winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized },
        { L"R16G16B16A16Float", winrt::DirectXPixelFormat::R16G16B16A16Float }
    };

    CreateControls(instance);
}

SampleWindow::~SampleWindow()
{
    m_windows.reset();
}

LRESULT SampleWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        auto command = HIWORD(wparam);
        auto hwnd = (HWND)lparam;
        switch (command)
        {
        case CBN_SELCHANGE:
            {
                auto index = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
                if (hwnd == m_windowComboBoxHwnd)
                {
                    auto window = m_windows->GetCurrentWindows()[index];
                    auto item = m_app->StartCaptureFromWindowHandle(window.WindowHandle);
                    OnCaptureStarted(item, CaptureType::ProgrammaticWindow);
                }
                else if (hwnd == m_monitorComboBoxHwnd)
                {
                    auto monitor = m_monitors->GetCurrentMonitors()[index];
                    auto item = m_app->StartCaptureFromMonitorHandle(monitor.MonitorHandle);
                    OnCaptureStarted(item, CaptureType::ProgrammaticMonitor);
                }
                else if (hwnd == m_pixelFormatComboBoxHwnd)
                {
                    auto pixelFormatData = m_pixelFormats[index];
                    m_app->PixelFormat(pixelFormatData.PixelFormat);
                }
            }
            break;
        case BN_CLICKED:
            {
                if (hwnd == m_pickerButtonHwnd)
                {
                    OnPickerButtonClicked();
                }
                else if (hwnd == m_stopButtonHwnd)
                {
                    StopCapture();
                }
                else if (hwnd == m_snapshotButtonHwnd)
                {
                    OnSnapshotButtonClicked();
                }
                else if (hwnd == m_cursorCheckBoxHwnd)
                {
                    auto value = SendMessageW(m_cursorCheckBoxHwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    m_app->IsCursorEnabled(value);
                }
                else if (hwnd == m_captureExcludeCheckBoxHwnd)
                {
                    auto value = SendMessageW(m_captureExcludeCheckBoxHwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    winrt::check_bool(SetWindowDisplayAffinity(m_window, value ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE));
                }
                else if (hwnd == m_borderRequiredCheckBoxHwnd)
                {
                    auto value = SendMessageW(m_borderRequiredCheckBoxHwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    m_app->IsBorderRequired(value);
                }
            }
            break;
        }
    }
    break;
    case WM_DISPLAYCHANGE:
        m_monitors->Update();
        break;
    case WM_CTLCOLORSTATIC:
    {
        HDC staticColorHdc = reinterpret_cast<HDC>(wparam);
        auto color = static_cast<COLORREF>(GetSysColor(COLOR_WINDOW));
        SetBkColor(staticColorHdc, color);
        SetDCBrushColor(staticColorHdc, color);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    break;
    default:
        return base_type::MessageHandler(message, wparam, lparam);
        break;
    }

    return 0;
}

void SampleWindow::OnCaptureStarted(winrt::GraphicsCaptureItem const& item, CaptureType captureType)
{
    m_itemClosedRevoker.revoke();
    m_itemClosedRevoker = item.Closed(winrt::auto_revoke, { this, &SampleWindow::OnCaptureItemClosed });
    SetSubTitle(std::wstring(item.DisplayName()));
    switch (captureType)
    {
    case CaptureType::ProgrammaticWindow:
        SendMessageW(m_monitorComboBoxHwnd, CB_SETCURSEL, -1, 0);
        break;
    case CaptureType::ProgrammaticMonitor:
        SendMessageW(m_windowComboBoxHwnd, CB_SETCURSEL, -1, 0);
        break;
    case CaptureType::Picker:
        SendMessageW(m_windowComboBoxHwnd, CB_SETCURSEL, -1, 0);
        SendMessageW(m_monitorComboBoxHwnd, CB_SETCURSEL, -1, 0);
        break;
    }
    SendMessageW(m_cursorCheckBoxHwnd, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(m_borderRequiredCheckBoxHwnd, BM_SETCHECK, BST_CHECKED, 0);
    EnableWindow(m_stopButtonHwnd, true);
    EnableWindow(m_snapshotButtonHwnd, true);
}

winrt::fire_and_forget SampleWindow::OnPickerButtonClicked()
{
    auto selectedItem = co_await m_app->StartCaptureWithPickerAsync();

    if (selectedItem)
    {
        OnCaptureStarted(selectedItem, CaptureType::Picker);
    }
}

winrt::fire_and_forget SampleWindow::OnSnapshotButtonClicked()
{
    auto file = co_await m_app->TakeSnapshotAsync();
    if (file != nullptr)
    {
        co_await winrt::Launcher::LaunchFileAsync(file);
    }
}

// Not DPI aware but could be by multiplying the constants based on the monitor scale factor
void SampleWindow::CreateControls(HINSTANCE instance)
{
    // Programmatic capture
    auto isWin32ProgrammaticPresent = winrt::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 8);
    auto win32ProgrammaticStyle = isWin32ProgrammaticPresent ? 0 : WS_DISABLED;

    // Cursor capture
    auto isCursorEnablePresent = winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 9);
    auto cursorEnableStyle = isCursorEnablePresent ? 0 : WS_DISABLED;

    // Window exclusion
    auto isWin32CaptureExcludePresent = winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 9);

    // Border configuration
    auto isBorderRequiredPresent = winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired");
    auto borderEnableSytle = isBorderRequiredPresent ? 0 : WS_DISABLED;

    auto controls = StackPanel(m_window, instance, 10, 10, 200);

    auto windowLabel = controls.CreateControl(ControlType::Label, L"Windows:");

    // Create window combo box
    HWND windowComboBoxHwnd = controls.CreateControl(ControlType::ComboBox, L"", win32ProgrammaticStyle);

    // Populate window combo box and register for updates
    m_windows->RegisterComboBoxForUpdates(windowComboBoxHwnd);

    auto monitorLabel = controls.CreateControl(ControlType::Label, L"Displays:");

    // Create monitor combo box
    HWND monitorComboBoxHwnd = controls.CreateControl(ControlType::ComboBox, L"", win32ProgrammaticStyle);

    // Populate monitor combo box
    m_monitors->RegisterComboBoxForUpdates(monitorComboBoxHwnd);

    // Create picker button
    HWND pickerButtonHwnd = controls.CreateControl(ControlType::Button, L"Open Picker");

    // Create stop capture button
    HWND stopButtonHwnd = controls.CreateControl(ControlType::Button, L"Stop Capture", WS_DISABLED);

    // Create independent snapshot button
    HWND snapshotButtonHwnd = controls.CreateControl(ControlType::Button, L"Take Snapshot", WS_DISABLED);

    auto pixelFormatLabel = controls.CreateControl(ControlType::Label, L"Pixel Format:");

   // Create pixel format combo box
    HWND pixelFormatComboBox = controls.CreateControl(ControlType::ComboBox, L"");

    // Populate pixel format combo box
    for (auto& pixelFormat : m_pixelFormats)
    {
        SendMessageW(pixelFormatComboBox, CB_ADDSTRING, 0, (LPARAM)pixelFormat.Name.c_str());
    }
    
    // The default pixel format is BGRA8
    SendMessageW(pixelFormatComboBox, CB_SETCURSEL, 0, 0);
  
    // Create cursor checkbox
    HWND cursorCheckBoxHwnd = controls.CreateControl(ControlType::CheckBox, L"Enable Cursor", cursorEnableStyle);

    // The default state is true for cursor rendering
    SendMessageW(cursorCheckBoxHwnd, BM_SETCHECK, BST_CHECKED, 0);

    // Create capture exclude checkbox
    // NOTE: We don't version check this feature because setting WDA_EXCLUDEFROMCAPTURE is the same as
    //       setting WDA_MONITOR on older builds of Windows. We're changing the label here to try and 
    //       limit any user confusion.
    std::wstring excludeCheckBoxLabel = isWin32CaptureExcludePresent ? L"Exclude this window" : L"Block this window";
    HWND captureExcludeCheckBoxHwnd = controls.CreateControl(ControlType::CheckBox, excludeCheckBoxLabel.c_str());

    // The default state is false for capture exclusion
    SendMessageW(captureExcludeCheckBoxHwnd, BM_SETCHECK, BST_UNCHECKED, 0);

    // Border required checkbox
    HWND borderRequiredCheckBoxHwnd = CreateWindowW(WC_BUTTON, L"Border required",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | borderEnableSytle,
        10, 320, 200, 30, m_window, nullptr, instance, nullptr);
    WINRT_VERIFY(borderRequiredCheckBoxHwnd);

    // The default state is false for border required checkbox
    SendMessageW(borderRequiredCheckBoxHwnd, BM_SETCHECK, BST_CHECKED, 0);

    m_windowComboBoxHwnd = windowComboBoxHwnd;
    m_monitorComboBoxHwnd = monitorComboBoxHwnd;
    m_pickerButtonHwnd = pickerButtonHwnd;
    m_stopButtonHwnd = stopButtonHwnd;
    m_snapshotButtonHwnd = snapshotButtonHwnd;
    m_cursorCheckBoxHwnd = cursorCheckBoxHwnd;
    m_captureExcludeCheckBoxHwnd = captureExcludeCheckBoxHwnd;
    m_pixelFormatComboBoxHwnd = pixelFormatComboBox;
    m_borderRequiredCheckBoxHwnd = borderRequiredCheckBoxHwnd;
}

void SampleWindow::SetSubTitle(std::wstring const& text)
{
    std::wstring titleText(L"Win32CaptureSample");
    if (!text.empty())
    {
        titleText += (L" - " + text);
    }
    SetWindowTextW(m_window, titleText.c_str());
}

void SampleWindow::StopCapture()
{
    m_app->StopCapture();
    SetSubTitle(L"");
    SendMessageW(m_windowComboBoxHwnd, CB_SETCURSEL, -1, 0);
    SendMessageW(m_monitorComboBoxHwnd, CB_SETCURSEL, -1, 0);
    SendMessageW(m_cursorCheckBoxHwnd, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(m_borderRequiredCheckBoxHwnd, BM_SETCHECK, BST_CHECKED, 0);
    EnableWindow(m_stopButtonHwnd, false);
    EnableWindow(m_snapshotButtonHwnd, false);
}

void SampleWindow::OnCaptureItemClosed(winrt::GraphicsCaptureItem const&, winrt::Windows::Foundation::IInspectable const&)
{
    StopCapture();
}