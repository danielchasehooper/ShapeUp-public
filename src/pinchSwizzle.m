#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <Appkit/AppKit.h>

float magnification = 0;

@implementation NSWindow (PinchGestureSwizzle)

- (void)swizzled_sendEvent:(NSEvent *)event {
    if (event.type == NSEventTypeMagnify) {
        magnification += event.magnification;
    }
    
    // Call the original implementation
    [self swizzled_sendEvent:event];
}

@end

void swizzleWindow(void) {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        Class class = [NSWindow class];

        Method originalMethod = class_getInstanceMethod(class, @selector(sendEvent:));
        Method swizzledMethod = class_getInstanceMethod(class, @selector(swizzled_sendEvent:));
        
        method_exchangeImplementations(originalMethod, swizzledMethod);
    });
}

void makeWindowKey(void) {
    [[[NSApplication sharedApplication].windows firstObject] makeKeyAndOrderFront:nil];
}

