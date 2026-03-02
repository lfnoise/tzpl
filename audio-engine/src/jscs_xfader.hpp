//
//  jscs_xfader.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef jscs_xfader_hpp
#define jscs_xfader_hpp

#include "jscs_client_interface.hpp"


namespace engine {

struct Engine;
struct Silo;
struct OutPort;
struct InPort;
struct Node;

Node* newXFaderNode(Engine* e, Silo* silo, f64 xfadeTime, FadeCurve curve, jscs_SignalType type);
jscs_SErr setupXFader(Silo* s, Node* sub, OutPort* newSrc, InPort* dst, FadeCurve curve, void* values);
jscs_SErr setupXFader(Silo* s, Node* sub, OutPort* oldSrc, OutPort* newSrc, FadeCurve curve);
}

#endif /* jscs_xfader_hpp */
