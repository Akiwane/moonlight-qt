// SPDX-License-Identifier: GPL-3.0-or-later

#include "jahdesk_toolbar.h"

#include <SDL_syswm.h>
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

namespace {
char ControllerKey;
NSString* const MouseItem = @"JAHDesk.MouseMode";
NSString* const CursorItem = @"JAHDesk.LocalCursor";
NSString* const ResolutionItem = @"JAHDesk.Resolution";
NSString* const DisconnectItem = @"JAHDesk.Disconnect";

struct Resolution {
    int width;
    int height;
};

constexpr Resolution Presets[] = {
    {1280, 720}, {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160},
};
constexpr NSInteger PresetCount = sizeof(Presets) / sizeof(Presets[0]);

// 获取 SDL 持有的 Cocoa 窗口。非 Cocoa 后端不使用这一工具栏。
NSWindow* cocoaWindow(SDL_Window* window)
{
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA) {
        return nil;
    }
    return info.info.cocoa.window;
}

// 解析宽高输入框中的十进制像素值，拒绝非数字和超出四位的内容。
int readDimension(NSTextField* field)
{
    NSString* value = field.stringValue;
    if (value.length == 0 || value.length > 4) {
        return 0;
    }
    int pixels = 0;
    for (NSUInteger index = 0; index < value.length; ++index) {
        const unichar digit = [value characterAtIndex:index];
        if (digit < '0' || digit > '9') {
            return 0;
        }
        pixels = pixels * 10 + digit - '0';
    }
    return pixels;
}

// 行控件采用固定布局，返回供模态面板读取的文本框。
NSTextField* addDimensionField(NSView* form, NSString* label, int pixels, CGFloat top)
{
    NSTextField* description = [NSTextField labelWithString:label];
    description.frame = NSMakeRect(0, top - 2, 62, 22);
    [form addSubview:description];
    NSTextField* input = [[[NSTextField alloc] initWithFrame:NSMakeRect(70, top, 130, 23)] autorelease];
    input.stringValue = [NSString stringWithFormat:@"%d", pixels];
    input.accessibilityLabel = label;
    [form addSubview:input];
    return input;
}
}

@interface JAHDeskMacToolbar : NSObject <NSToolbarDelegate>
{
    SDL_Window* _streamWindow;
    Uint32 _eventType;
    BOOL _absolute;
    BOOL _cursorVisible;
    int _width;
    int _height;
}
- (instancetype)initWithWindow:(SDL_Window*)window eventType:(Uint32)eventType;
- (void)updateToolbar:(NSToolbar*)toolbar absolute:(BOOL)absolute cursorVisible:(BOOL)cursorVisible
                width:(int)width height:(int)height;
@end

@implementation JAHDeskMacToolbar

// 记录所属串流窗口和该会话的 SDL 工具栏事件类型。
- (instancetype)initWithWindow:(SDL_Window*)window eventType:(Uint32)eventType
{
    self = [super init];
    if (self != nil) {
        _streamWindow = window;
        _eventType = eventType;
    }
    return self;
}

// 返回禁止自定义的工具栏项目，保证断开按钮可用。
- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar
{
    (void)toolbar;
    return @[MouseItem, CursorItem, ResolutionItem, NSToolbarFlexibleSpaceItemIdentifier, DisconnectItem];
}

// 返回按钮从左到右的初始顺序。
- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar
{
    return [self toolbarAllowedItemIdentifiers:toolbar];
}

// 将鼠标模式变更交给串流循环处理。
- (void)changeMouseMode:(id)sender
{
    (void)sender;
    JAHDeskToolbar::push(_streamWindow, _eventType, JAHDeskToolbar::Action::ToggleMouseMode);
}

// 将本地光标变更交给串流循环处理。
- (void)changeLocalCursor:(id)sender
{
    (void)sender;
    JAHDeskToolbar::push(_streamWindow, _eventType, JAHDeskToolbar::Action::ToggleLocalCursor);
}

// 读取项目对应的宽高，并请求串流循环重新协商分辨率。
- (void)changeResolution:(NSMenuItem*)sender
{
    const Resolution selected = Presets[sender.tag];
    JAHDeskToolbar::push(_streamWindow, _eventType, JAHDeskToolbar::Action::SetResolution,
                         selected.width, selected.height);
}

// 由串流线程先释放远端输入，再显示本机的自定义尺寸面板。
- (void)openResolution:(id)sender
{
    (void)sender;
    JAHDeskToolbar::push(_streamWindow, _eventType, JAHDeskToolbar::Action::OpenResolution);
}

// 请求串流循环正常断开。
- (void)disconnect:(id)sender
{
    (void)sender;
    JAHDeskToolbar::push(_streamWindow, _eventType, JAHDeskToolbar::Action::Disconnect);
}

// 创建会显示当前值的分辨率下拉菜单。
- (NSPopUpButton*)resolutionMenu
{
    NSPopUpButton* menu = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 163, 27)
                                                   pullsDown:YES] autorelease];
    [menu addItemWithTitle:@"分辨率"];
    for (NSInteger i = 0; i < PresetCount; ++i) {
        const Resolution preset = Presets[i];
        [menu addItemWithTitle:[NSString stringWithFormat:@"%d × %d", preset.width, preset.height]];
        NSMenuItem* item = menu.lastItem;
        item.target = self;
        item.action = @selector(changeResolution:);
        item.tag = i;
    }
    [menu.menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"自定义…"];
    menu.lastItem.target = self;
    menu.lastItem.action = @selector(openResolution:);
    return menu;
}

// 将串流的实际设置同步到工具栏的标签、可用状态与选中项。
- (void)refreshItem:(NSToolbarItem*)item
{
    if ([item.itemIdentifier isEqualToString:MouseItem]) {
        NSButton* button = static_cast<NSButton*>(item.view);
        button.title = _absolute ? @"鼠标：主控端" : @"鼠标：相对移动";
        item.menuFormRepresentation.title = button.title;
    } else if ([item.itemIdentifier isEqualToString:CursorItem]) {
        NSButton* button = static_cast<NSButton*>(item.view);
        button.title = _cursorVisible ? @"本地光标：显示" : @"本地光标：隐藏";
        button.enabled = _absolute;
        item.menuFormRepresentation.title = button.title;
        item.menuFormRepresentation.enabled = _absolute;
    } else if ([item.itemIdentifier isEqualToString:ResolutionItem]) {
        NSPopUpButton* menu = static_cast<NSPopUpButton*>(item.view);
        [menu itemAtIndex:0].title = _width > 0 && _height > 0 ?
            [NSString stringWithFormat:@"分辨率 %d × %d", _width, _height] : @"分辨率";
        item.menuFormRepresentation.title = [menu itemAtIndex:0].title;
        for (NSInteger i = 1; i <= PresetCount; ++i) {
            const Resolution preset = Presets[i - 1];
            const NSControlStateValue state = preset.width == _width && preset.height == _height ?
                NSControlStateValueOn : NSControlStateValueOff;
            [menu itemAtIndex:i].state = state;
            [item.menuFormRepresentation.submenu itemAtIndex:i - 1].state = state;
        }
    }
}

// 为指定的原生项目安装本地控制。SDL 的画面继续独占内容视图。
- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
    itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
willBeInsertedIntoToolbar:(BOOL)willBeInserted
{
    (void)toolbar;
    (void)willBeInserted;
    NSToolbarItem* item = [[[NSToolbarItem alloc] initWithItemIdentifier:identifier] autorelease];
    if ([identifier isEqualToString:ResolutionItem]) {
        item.label = @"分辨率";
        item.toolTip = @"选择分辨率并重连";
        item.view = [self resolutionMenu];
        NSMenuItem* overflow = [[[NSMenuItem alloc] initWithTitle:item.label action:nil keyEquivalent:@""] autorelease];
        NSMenu* submenu = [[[[self resolutionMenu] menu] copy] autorelease];
        [submenu removeItemAtIndex:0];
        overflow.submenu = submenu;
        item.menuFormRepresentation = overflow;
    } else {
        NSString* label;
        SEL action;
        CGFloat width;
        if ([identifier isEqualToString:MouseItem]) {
            label = @"鼠标模式";
            action = @selector(changeMouseMode:);
            width = 112;
        } else if ([identifier isEqualToString:CursorItem]) {
            label = @"本地光标";
            action = @selector(changeLocalCursor:);
            width = 116;
        } else if ([identifier isEqualToString:DisconnectItem]) {
            label = @"断开";
            action = @selector(disconnect:);
            width = 62;
            item.visibilityPriority = NSToolbarItemVisibilityPriorityUser;
        } else {
            return nil;
        }
        NSButton* button = [NSButton buttonWithTitle:label target:self action:action];
        button.frame = NSMakeRect(0, 0, width, 27);
        button.bezelStyle = NSBezelStyleRounded;
        item.label = label;
        item.view = button;
        NSMenuItem* overflow = [[[NSMenuItem alloc] initWithTitle:label action:action keyEquivalent:@""] autorelease];
        overflow.target = self;
        item.menuFormRepresentation = overflow;
    }
    [self refreshItem:item];
    return item;
}

// 保存会话状态，并刷新当前窗口的所有工具栏按钮。
- (void)updateToolbar:(NSToolbar*)toolbar absolute:(BOOL)absolute cursorVisible:(BOOL)cursorVisible
                width:(int)width height:(int)height
{
    _absolute = absolute;
    _cursorVisible = cursorVisible;
    _width = width;
    _height = height;
    for (NSToolbarItem* item in toolbar.items) {
        [self refreshItem:item];
    }
}

@end

namespace JAHDeskToolbar {

// 在窗口内容区上方挂载原生工具栏，保持 SDL 的渲染及远端输入坐标。
bool attach(SDL_Window* window, Uint32 eventType, bool absolute, bool cursorVisible, int width, int height)
{
    @autoreleasepool {
        NSWindow* nativeWindow = cocoaWindow(window);
        if (nativeWindow == nil || nativeWindow.toolbar != nil ||
            !(nativeWindow.styleMask & NSWindowStyleMaskTitled)) {
            return false;
        }
        JAHDeskMacToolbar* controller = [[[JAHDeskMacToolbar alloc] initWithWindow:window
                                                                         eventType:eventType] autorelease];
        NSToolbar* toolbar = [[[NSToolbar alloc] initWithIdentifier:@"JAHDesk.StreamToolbar"] autorelease];
        toolbar.allowsUserCustomization = NO;
        toolbar.autosavesConfiguration = NO;
        toolbar.displayMode = NSToolbarDisplayModeIconOnly;
        [controller updateToolbar:toolbar absolute:absolute cursorVisible:cursorVisible width:width height:height];
        toolbar.delegate = controller;
        objc_setAssociatedObject(nativeWindow, &ControllerKey, controller, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if (@available(macOS 11.0, *)) {
            nativeWindow.toolbarStyle = NSWindowToolbarStyleExpanded;
        }
        nativeWindow.toolbar = toolbar;
        return true;
    }
}

// 更新同一个窗口的工具栏，供远端热键和本地按钮共用。
void update(SDL_Window* window, bool absolute, bool cursorVisible, int width, int height)
{
    @autoreleasepool {
        NSWindow* nativeWindow = cocoaWindow(window);
        if (nativeWindow == nil) {
            return;
        }
        auto* controller = static_cast<JAHDeskMacToolbar*>(objc_getAssociatedObject(nativeWindow, &ControllerKey));
        [controller updateToolbar:nativeWindow.toolbar absolute:absolute cursorVisible:cursorVisible width:width height:height];
    }
}

// 先拆工具栏再释放其代理，窗口销毁时不会继续引用 SDL 对象。
void detach(SDL_Window* window)
{
    @autoreleasepool {
        NSWindow* nativeWindow = cocoaWindow(window);
        if (nativeWindow == nil || objc_getAssociatedObject(nativeWindow, &ControllerKey) == nil) {
            return;
        }
        nativeWindow.toolbar.delegate = nil;
        nativeWindow.toolbar = nil;
        objc_setAssociatedObject(nativeWindow, &ControllerKey, nil, OBJC_ASSOCIATION_ASSIGN);
    }
}

// 此函数由已暂停远端输入的串流循环调用。应用时回填宽高，取消时保留原值。
bool promptResolution(SDL_Window* window, int& width, int& height)
{
    @autoreleasepool {
        if (cocoaWindow(window) == nil) {
            return false;
        }
        NSAlert* alert = [[[NSAlert alloc] init] autorelease];
        alert.messageText = @"自定义分辨率";
        alert.informativeText = @"输入 256 至 8192 之间的偶数像素。";
        [alert addButtonWithTitle:@"应用并重连"];
        [alert addButtonWithTitle:@"取消"];
        NSView* form = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 204, 59)] autorelease];
        NSTextField* widthField = addDimensionField(form, @"宽度", width, 34);
        NSTextField* heightField = addDimensionField(form, @"高度", height, 2);
        alert.accessoryView = form;
        NSTextField* nextField = widthField;
        for (;;) {
            alert.window.initialFirstResponder = nextField;
            if ([alert runModal] != NSAlertFirstButtonReturn) {
                return false;
            }
            const int requestedWidth = readDimension(widthField);
            const int requestedHeight = readDimension(heightField);
            if (validResolution(requestedWidth, requestedHeight)) {
                width = requestedWidth;
                height = requestedHeight;
                return true;
            }
            alert.informativeText = @"宽度和高度均须为 256 至 8192 之间的偶数。";
            nextField = validResolution(requestedWidth, requestedWidth) ? heightField : widthField;
        }
    }
}

}
