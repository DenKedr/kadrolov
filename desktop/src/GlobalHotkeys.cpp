#include "GlobalHotkeys.h"
#include <QCoreApplication>
#include <QSettings>
#include <QSocketNotifier>
#include <QSet>
static int combined(const QKeySequence &s){
 if(s.isEmpty())return 0;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
 return s[0].toCombined();
#else
 return s[0];
#endif
}
static int keyOf(const QKeySequence&s){return combined(s)&~int(Qt::KeyboardModifierMask);}
#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
struct Keys{EventHotKeyRef refs[2]={};EventHandlerRef handler=nullptr;};
static OSStatus callback(EventHandlerCallRef,EventRef event,void* context){EventHotKeyID key{};GetEventParameter(event,kEventParamDirectObject,typeEventHotKeyID,nullptr,sizeof(key),nullptr,&key);auto *o=static_cast<GlobalHotkeys*>(context);if(key.id==1)emit o->screenshotRequested();if(key.id==2)emit o->recordingRequested();return noErr;}
static int nativeKey(int k){
 static const int letters[]={kVK_ANSI_A,kVK_ANSI_B,kVK_ANSI_C,kVK_ANSI_D,kVK_ANSI_E,kVK_ANSI_F,kVK_ANSI_G,kVK_ANSI_H,kVK_ANSI_I,kVK_ANSI_J,kVK_ANSI_K,kVK_ANSI_L,kVK_ANSI_M,kVK_ANSI_N,kVK_ANSI_O,kVK_ANSI_P,kVK_ANSI_Q,kVK_ANSI_R,kVK_ANSI_S,kVK_ANSI_T,kVK_ANSI_U,kVK_ANSI_V,kVK_ANSI_W,kVK_ANSI_X,kVK_ANSI_Y,kVK_ANSI_Z};
 static const int digits[]={kVK_ANSI_0,kVK_ANSI_1,kVK_ANSI_2,kVK_ANSI_3,kVK_ANSI_4,kVK_ANSI_5,kVK_ANSI_6,kVK_ANSI_7,kVK_ANSI_8,kVK_ANSI_9};
 static const int fs[]={kVK_F1,kVK_F2,kVK_F3,kVK_F4,kVK_F5,kVK_F6,kVK_F7,kVK_F8,kVK_F9,kVK_F10,kVK_F11,kVK_F12,kVK_F13,kVK_F14,kVK_F15,kVK_F16,kVK_F17,kVK_F18,kVK_F19,kVK_F20};
 if(k>=Qt::Key_A&&k<=Qt::Key_Z)return letters[k-Qt::Key_A];if(k>=Qt::Key_0&&k<=Qt::Key_9)return digits[k-Qt::Key_0];if(k>=Qt::Key_F1&&k<=Qt::Key_F20)return fs[k-Qt::Key_F1];if(k==Qt::Key_Space)return kVK_Space;return -1;
}
static unsigned modifiers(const QKeySequence&s){int c=combined(s);return ((c&Qt::CTRL)?cmdKey:0)|((c&Qt::META)?controlKey:0)|((c&Qt::ALT)?optionKey:0)|((c&Qt::SHIFT)?shiftKey:0);}
#elif defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
static int nativeKey(int k){if((k>=Qt::Key_A&&k<=Qt::Key_Z)||(k>=Qt::Key_0&&k<=Qt::Key_9))return k;if(k>=Qt::Key_F1&&k<=Qt::Key_F24)return VK_F1+k-Qt::Key_F1;if(k==Qt::Key_Print)return VK_SNAPSHOT;if(k==Qt::Key_Pause)return VK_PAUSE;if(k==Qt::Key_Space)return VK_SPACE;return -1;}
static unsigned modifiers(const QKeySequence&s){int c=combined(s);return MOD_NOREPEAT|((c&Qt::CTRL)?MOD_CONTROL:0)|((c&Qt::META)?MOD_WIN:0)|((c&Qt::ALT)?MOD_ALT:0)|((c&Qt::SHIFT)?MOD_SHIFT:0);}
#elif defined(KADROLOV_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
struct Keys{Display *d=nullptr;QSocketNotifier *notifier=nullptr;unsigned code[2]={},mods[2]={};QSet<unsigned>pressed;QList<Window>roots;};
static Display *grabDisplay=nullptr;static int grabError=0;static XErrorHandler oldHandler=nullptr;
static int grabHandler(Display*d,XErrorEvent*e){if(d==grabDisplay){grabError=e->error_code;return 0;}return oldHandler?oldHandler(d,e):0;}
static unsigned modifiers(const QKeySequence&s){int c=combined(s);return ((c&Qt::CTRL)?ControlMask:0)|((c&Qt::META)?Mod4Mask:0)|((c&Qt::ALT)?Mod1Mask:0)|((c&Qt::SHIFT)?ShiftMask:0);}
static KeySym nativeKey(int k){if(k>=Qt::Key_A&&k<=Qt::Key_Z)return XK_a+k-Qt::Key_A;if(k>=Qt::Key_0&&k<=Qt::Key_9)return XK_0+k-Qt::Key_0;if(k>=Qt::Key_F1&&k<=Qt::Key_F24)return XK_F1+k-Qt::Key_F1;if(k==Qt::Key_Print)return XK_Print;if(k==Qt::Key_Pause)return XK_Pause;if(k==Qt::Key_Space)return XK_space;return NoSymbol;}
#endif
QKeySequence GlobalHotkeys::defaultScreenshot(){
#ifdef Q_OS_MACOS
 return QKeySequence("Meta+Alt+S");
#else
 return QKeySequence(Qt::Key_Print);
#endif
}
QKeySequence GlobalHotkeys::defaultVideo(){
#ifdef Q_OS_MACOS
 return QKeySequence("Meta+Alt+V");
#else
 return QKeySequence("Ctrl+Shift+Print");
#endif
}
GlobalHotkeys::GlobalHotkeys(QObject*p):QObject(p){
 screenshot_=QKeySequence(QSettings().value("hotkeys/screenshot",defaultScreenshot().toString(QKeySequence::PortableText)).toString(),QKeySequence::PortableText);
 video_=QKeySequence(QSettings().value("hotkeys/video",defaultVideo().toString(QKeySequence::PortableText)).toString(),QKeySequence::PortableText);bind();
}
void GlobalHotkeys::release(){
#ifdef Q_OS_MACOS
 auto*k=static_cast<Keys*>(native_);if(k){for(auto r:k->refs)if(r)UnregisterEventHotKey(r);if(k->handler)RemoveEventHandler(k->handler);delete k;}
#elif defined(Q_OS_WIN)
 if(screenshotAvailable_)UnregisterHotKey(nullptr,0x4b01);if(recordingAvailable_)UnregisterHotKey(nullptr,0x4b02);QCoreApplication::instance()->removeNativeEventFilter(this);
#elif defined(KADROLOV_HAVE_X11)
 auto*k=static_cast<Keys*>(native_);if(k){delete k->notifier;for(auto root:k->roots)for(int i=0;i<2;++i)if(k->code[i])for(unsigned lock:{0u,unsigned(LockMask),unsigned(Mod2Mask),unsigned(LockMask|Mod2Mask)})XUngrabKey(k->d,k->code[i],k->mods[i]|lock,root);XCloseDisplay(k->d);delete k;}
#endif
 native_=nullptr;screenshotAvailable_=recordingAvailable_=false;
}
GlobalHotkeys::~GlobalHotkeys(){release();}
bool GlobalHotkeys::bind(){
#ifdef Q_OS_MACOS
 auto*k=new Keys;native_=k;EventTypeSpec spec{kEventClassKeyboard,kEventHotKeyPressed};if(InstallApplicationEventHandler(callback,1,&spec,this,&k->handler)!=noErr)return false;
 const QKeySequence seq[]={screenshot_,video_};bool available[2]={};for(int i=0;i<2;++i){int n=nativeKey(keyOf(seq[i]));if(n>=0)available[i]=RegisterEventHotKey(n,modifiers(seq[i]),EventHotKeyID{'KADR',UInt32(i+1)},GetApplicationEventTarget(),0,&k->refs[i])==noErr;}screenshotAvailable_=available[0];recordingAvailable_=available[1];
#elif defined(Q_OS_WIN)
 int s=nativeKey(keyOf(screenshot_)),v=nativeKey(keyOf(video_));screenshotAvailable_=s>=0&&RegisterHotKey(nullptr,0x4b01,modifiers(screenshot_),s);recordingAvailable_=v>=0&&RegisterHotKey(nullptr,0x4b02,modifiers(video_),v);QCoreApplication::instance()->installNativeEventFilter(this);
#elif defined(KADROLOV_HAVE_X11)
 if(qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))return false;auto*d=XOpenDisplay(nullptr);if(!d)return false;auto*k=new Keys;native_=k;k->d=d;Bool repeat=False;XkbSetDetectableAutoRepeat(d,True,&repeat);for(int i=0;i<ScreenCount(d);++i)k->roots<<RootWindow(d,i);
 const QKeySequence seq[]={screenshot_,video_};bool available[2]={};
 for(int i=0;i<2;++i){k->mods[i]=modifiers(seq[i]);k->code[i]=XKeysymToKeycode(d,nativeKey(keyOf(seq[i])));if(!k->code[i])continue;XSync(d,False);grabDisplay=d;grabError=0;oldHandler=XSetErrorHandler(grabHandler);for(auto root:k->roots)for(unsigned lock:{0u,unsigned(LockMask),unsigned(Mod2Mask),unsigned(LockMask|Mod2Mask)})XGrabKey(d,k->code[i],k->mods[i]|lock,root,False,GrabModeAsync,GrabModeAsync);XSync(d,False);available[i]=!grabError;XSetErrorHandler(oldHandler);grabDisplay=nullptr;if(!available[i]){for(auto root:k->roots)for(unsigned lock:{0u,unsigned(LockMask),unsigned(Mod2Mask),unsigned(LockMask|Mod2Mask)})XUngrabKey(d,k->code[i],k->mods[i]|lock,root);k->code[i]=0;}}
 screenshotAvailable_=available[0];recordingAvailable_=available[1];k->notifier=new QSocketNotifier(ConnectionNumber(d),QSocketNotifier::Read,this);connect(k->notifier,&QSocketNotifier::activated,this,[this,k]{while(XPending(k->d)){XEvent e{};XNextEvent(k->d,&e);if(e.type==KeyRelease){if(XPending(k->d)){XEvent next{};XPeekEvent(k->d,&next);if(next.type==KeyPress&&next.xkey.keycode==e.xkey.keycode&&next.xkey.time==e.xkey.time)continue;}k->pressed.remove(e.xkey.keycode);}if(e.type!=KeyPress||k->pressed.contains(e.xkey.keycode))continue;k->pressed.insert(e.xkey.keycode);const unsigned m=e.xkey.state&~(LockMask|Mod2Mask);if(screenshotAvailable_&&e.xkey.keycode==k->code[0]&&m==k->mods[0])emit screenshotRequested();if(recordingAvailable_&&e.xkey.keycode==k->code[1]&&m==k->mods[1])emit recordingRequested();}});
#endif
 return screenshotAvailable_&&recordingAvailable_;
}
bool GlobalHotkeys::setShortcuts(const QKeySequence&s,const QKeySequence&v,QString*error){
 if(s.isEmpty()||v.isEmpty()||s.count()!=1||v.count()!=1||s==v){if(error)*error=QStringLiteral("Выберите два разных сочетания из одной клавиши и модификаторов.");return false;}
 auto oldS=screenshot_,oldV=video_;release();screenshot_=s;video_=v;
 if(!bind()){release();screenshot_=oldS;video_=oldV;bind();if(error)*error=QStringLiteral("Сочетание занято системой, другой программой или не поддерживается. Предыдущие сочетания сохранены.");return false;}
 QSettings settings;settings.setValue("hotkeys/screenshot",s.toString(QKeySequence::PortableText));settings.setValue("hotkeys/video",v.toString(QKeySequence::PortableText));return true;
}
QString GlobalHotkeys::statusText()const{return QStringLiteral("Снимок: %1 — %2. Видео: %3 — %4.").arg(screenshot_.toString(QKeySequence::NativeText),screenshotAvailable_?QStringLiteral("работает глобально"):QStringLiteral("занято или недоступно"),video_.toString(QKeySequence::NativeText),recordingAvailable_?QStringLiteral("работает глобально"):QStringLiteral("занято или недоступно"));}
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
bool GlobalHotkeys::nativeEventFilter(const QByteArray&,void*message,qintptr*){
#else
bool GlobalHotkeys::nativeEventFilter(const QByteArray&,void*message,long*){
#endif
#ifdef Q_OS_WIN
 auto*m=static_cast<MSG*>(message);if(m->message==WM_HOTKEY){if(m->wParam==0x4b01)emit screenshotRequested();if(m->wParam==0x4b02)emit recordingRequested();return true;}
#else
 Q_UNUSED(message)
#endif
 return false;
}
