#ifndef CROSSHAIROVERLAY_H
#define CROSSHAIROVERLAY_H

// header 
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <thread>
#include <array> 
class CrosshairOverlay  {
        public:
        void run ()       ;   
         CrosshairOverlay ()       ;   
         ~CrosshairOverlay ()       ;   
        
        Display* getDisplay ()       ;   
        void setDisplay (Display* display)       ;   
        
        const int& getScreen ()       ;   
        void setScreen (int screen)       ;   
        
        const Window& getRoot ()       ;   
        void setRoot (Window root)       ;   
        
        const Window& getWindow ()       ;   
        void setWindow (Window window)       ;   
        
        const GC& getGc ()       ;   
        void setGc (GC gc)       ;   
        
        const GC& getBlackGc ()       ;   
        void setBlackGc (GC black_gc)       ;   
        private:
        Display* display {nullptr};
        int screen {0};
        Window root {0};
        Window window {0};
        GC gc {nullptr};
        GC black_gc {nullptr};
        int xi_opcode {0};
        bool xi2_enabled {false};
};

#endif /* !CROSSHAIROVERLAY_H */