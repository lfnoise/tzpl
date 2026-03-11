//
//  tzpl_test_plugins.hpp
//  audio engine
//
//  Test/example plugins: SinOsc, AddOp, MulOp, VoicerTest.
//

#ifndef tzpl_test_plugins_h
#define tzpl_test_plugins_h

#include "tzpl_client_interface.hpp"
#include "tzpl_node.hpp"
#include "tzpl_voicer.hpp"

namespace engine {

// Re-import shared voicer types into engine namespace
using ::ShouldCache;
using ::VoicesInRows;
using ::VoicesInColumns;
using ::Voicer;
using ::ColumnVoicer;
using ::RowVoicer;
using ::NextPowerOfTwo;
using ::NextPowerOfTwo_;
using ::nnhz;
using ::calcDecay;
using ::ampdb;

f32 bwarp(f32 x, f32 w);

// ---------------------------------------------------------------------------
// Plugin registration functions
// ---------------------------------------------------------------------------

void createSineNode(Engine* e);
void createAddOpNode(Engine* e);
void createMulOpNode(Engine* e);
void createVoicerTestNode(Engine* e);

} // namespace engine

#endif // tzpl_test_plugins_h
