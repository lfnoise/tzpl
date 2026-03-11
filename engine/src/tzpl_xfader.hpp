//
//  tzpl_xfader.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_xfader_hpp
#define tzpl_xfader_hpp

#include "tzpl_client_interface.hpp"


namespace engine {

struct Engine;
struct Silo;
struct OutPort;
struct InPort;
struct Node;

Node* newXFaderNode(Engine* e, Silo* silo, f64 xfadeTime, FadeCurve curve, tzpl_SignalType type);
tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* newSrc, InPort* dst, FadeCurve curve, void* values);
tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* oldSrc, OutPort* newSrc, FadeCurve curve);
}

#endif /* tzpl_xfader_hpp */
