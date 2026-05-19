%main = fn(%var_2: u8[1,3,480,640]): // (u8[1,3,480,640]) -> f32[1,6,6300]
{
  %TileAct1Case_0 = fusion<k230>(%var_182: u8[1,1,4,6300]): // (u8[1,1,4,6300]) -> f32[1,1,4,6300]
  {
    %0 = GNNELoad(%var_182: u8[1,1,4,6300]): // u8[1,1,4,6300]
    %1 = GNNELoad(const(f32[1,1,1,6300])): // f16[1,1,1,6300]
    %2 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)1,(Half)1,(Half)0,(Half)0,(Half)-Infinity,(Half)Infinity}}}})): // f16[1,1,1,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<0, 0.3118054>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : False)): // f16[1,1,4,6300]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // f32[1,1,4,6300]
  }
  %TileAct1Case_0 = fusion<k230>(%var_180: u8[1,1,2,6300], %var_181: u8[1,1,2,6300]): // (u8[1,1,2,6300], u8[1,1,2,6300]) -> u8[1,1,2,6300]
  {
    %0 = GNNELoad(%var_180: u8[1,1,2,6300]): // u8[1,1,2,6300]
    %1 = GNNELoad(%var_181: u8[1,1,2,6300]): // u8[1,1,2,6300]
    %2 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)1.604,(Half)1.604,(Half)0,(Half)0,(Half)0,(Half)255}}}})): // f16[1,1,1,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<25, 0.34365988>}), const(DeQParam : {<0, 0.33789083>}), const(i32 : 1), const(bool : False)): // u8[1,1,2,6300]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,1,2,6300]
  }
  %TileAct1Case_0 = fusion<k230>(%var_159: f32[1,1,2,6300]): // (f32[1,1,2,6300]) -> u8[1,1,2,6300]
  {
    %0 = GNNELoad(%var_159: f32[1,1,2,6300]): // f16[1,1,2,6300]
    %1 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)2.91,(Half)2.91,(Half)25,(Half)25,(Half)0,(Half)255}}}})): // f16[1,1,1,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<25, 0.34365988>}), const(DeQParam : {<25, 0.34365988>}), const(i32 : 1), const(bool : False)): // u8[1,1,2,6300]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,1,2,6300]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_78: u8[1,16,4,6300]): // (u8[1,16,4,6300]) -> f32[1,1,4,6300]
  {
    %0 = GNNELoad(%var_78: u8[1,16,4,6300]): // u8[1,16,4,6300]
    %1 = GNNELoadW(const(u8[1,16,1,1])): // u8[1,16,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,1] : {{{{0}}}})): // u8[1,1,1,1]
    %3 = GNNELoadW(const(u8[1] : {0})): // u8[1]
    %4 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)59200,(Half)59200,(Half)0,(Half)0,(Half)-Infinity,(Half)Infinity,(Half)0}}}})): // f16[1,1,1,7]
    %5 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)1,(Half)1,(Half)0,(Half)0,(Half)-Infinity,(Half)Infinity,(Half)0}}}})): // f16[1,1,1,7]
    %6 = GNNELoadW(const(u8[16])): // u8[16]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 0), const(i32 : 28), const(i32 : 0), const(DeQParam : <0, 0.0037482108>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 0), %6): // f16[1,1,4,6300]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f32[1,1,4,6300]
  }
  %TileAct1Case_0 = fusion<k230>(%var_158: f32[1,16,4,6300]): // (f32[1,16,4,6300]) -> u8[1,16,4,6300]
  {
    %0 = GNNELoad(%var_158: f32[1,16,4,6300]): // f16[1,16,4,6300]
    %1 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<0, 0.0037482108>}), const(DeQParam : {<0, 0.0037482108>}), const(i32 : 16), const(bool : False)): // u8[1,16,4,6300]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,16,4,6300]
  }
  %TileTransposeCase_0 = fusion<k230>(%var_79: f32[1,4,16,6300]): // (f32[1,4,16,6300]) -> f32[1,16,4,6300]
  {
    %0 = GNNELoad(%var_79: f32[1,4,16,6300]): // f16[1,4,16,6300]
    %1 = GNNETranspose(%0): // f16[1,16,4,6300]
    %2 = GNNEStore(%1, const(i64[4] : {1L,1L,1L,1L})): // f32[1,16,4,6300]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_50: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f32[1,64,60,80]
  {
    %0 = GNNELoad(%var_50: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 4), const(i32 : 28), const(i32 : 0), const(DeQParam : <4, 0.06971161>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 4), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f32[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_135: f16[1,64,60,80]): // (f16[1,64,60,80]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_135: f16[1,64,60,80]): // f16[1,64,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<109, 0.15850578>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_49: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,64,60,80]
  {
    %0 = GNNELoad(%var_49: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 15), const(i32 : 28), const(i32 : 0), const(DeQParam : <15, 0.019178126>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 15), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_134: f16[1,64,60,80]): // (f16[1,64,60,80]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_134: f16[1,64,60,80]): // f16[1,64,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<148, 0.05998471>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_48: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,64,60,80]
  {
    %0 = GNNELoad(%var_48: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 21), const(i32 : 30), const(i32 : 0), const(DeQParam : <21, 0.013036354>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 21), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_133: f16[1,64,60,80]): // (f16[1,64,60,80]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_133: f16[1,64,60,80]): // f16[1,64,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<144, 0.040018514>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_47: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,64,60,80]
  {
    %0 = GNNELoad(%var_47: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 19), const(i32 : 28), const(i32 : 0), const(DeQParam : <19, 0.01439757>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 19), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_131: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_131: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<142, 0.042082686>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_45: u8[1,32,60,80]): // (u8[1,32,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_45: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoadW(const(u8[32,32,3,3])): // u8[32,32,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[9216])): // u8[9216]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 16), const(i32 : 29), const(i32 : 0), const(DeQParam : <16, 0.017946696>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 16), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_130: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_130: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<132, 0.045243073>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_44: u8[1,32,60,80]): // (u8[1,32,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_44: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoadW(const(u8[32,32,1,1])): // u8[32,32,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[1024])): // u8[1024]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 21), const(i32 : 28), const(i32 : 0), const(DeQParam : <21, 0.013045896>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 21), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_129: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_129: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<151, 0.040008623>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_43: u8[1,128,60,80]): // (u8[1,128,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_43: u8[1,128,60,80]): // u8[1,128,60,80]
    %1 = GNNELoadW(const(u8[32,128,1,1])): // u8[32,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 15), const(i32 : 30), const(i32 : 0), const(DeQParam : <15, 0.018024918>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 15), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_128: u8[1,64,30,40]): // (u8[1,64,30,40]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_128: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<13, 0.022053791>}), const(DeQParam : {<0, 1>}), const(i32 : 64), const(bool : False)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_127: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_127: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<132, 0.045044262>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_42: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_42: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[64,128,1,1])): // u8[64,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[8192])): // u8[8192]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 14), const(i32 : 30), const(i32 : 0), const(DeQParam : <14, 0.019423788>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 14), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_126: f16[1,128,30,40]): // (f16[1,128,30,40]) -> u8[1,128,30,40]
  {
    %0 = GNNELoad(%var_126: f16[1,128,30,40]): // f16[1,128,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<143, 0.06049877>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_41: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,128,30,40]
  {
    %0 = GNNELoad(%var_41: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[128,128,1,1])): // u8[128,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[16384])): // u8[16384]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 16), const(i32 : 28), const(i32 : 0), const(DeQParam : <16, 0.017651044>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 16), %6): // f16[1,128,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_124: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_124: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<130, 0.04886666>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_39: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_39: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 15), const(i32 : 30), const(i32 : 0), const(DeQParam : <15, 0.019025313>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 15), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_123: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_123: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<142, 0.05507515>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_38: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_38: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 16), const(i32 : 28), const(i32 : 0), const(DeQParam : <16, 0.016976984>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 16), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_122: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_122: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<149, 0.053787984>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_37: u8[1,256,30,40]): // (u8[1,256,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_37: u8[1,256,30,40]): // u8[1,256,30,40]
    %1 = GNNELoadW(const(u8[64,256,1,1])): // u8[64,256,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[16384])): // u8[16384]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 28), const(i32 : 0), const(DeQParam : <12, 0.023318172>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_121: u8[1,128,15,20]): // (u8[1,128,15,20]) -> u8[1,128,30,40]
  {
    %0 = GNNELoad(%var_121: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<12, 0.023429077>}), const(DeQParam : {<0, 1>}), const(i32 : 128), const(bool : False)): // u8[1,128,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_120: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_120: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<138, 0.09439471>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_36: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_36: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[128,264,1,1])): // u8[128,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[32768])): // u8[32768]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 21), const(i32 : 29), const(i32 : 0), const(DeQParam : <21, 0.013511142>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 21), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_119: f16[1,256,15,20]): // (f16[1,256,15,20]) -> u8[1,256,15,20]
  {
    %0 = GNNELoad(%var_119: f16[1,256,15,20]): // f16[1,256,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<130, 0.050506957>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,256,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,256,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_35: u8[1,512,15,20]): // (u8[1,512,15,20]) -> f16[1,256,15,20]
  {
    %0 = GNNELoad(%var_35: u8[1,512,15,20]): // u8[1,512,15,20]
    %1 = GNNELoadW(const(u8[256,528,1,1])): // u8[256,528,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,256])): // u8[1,1,1,256]
    %3 = GNNELoadW(const(u8[256])): // u8[256]
    %4 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %5 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %6 = GNNELoadW(const(u8[131072])): // u8[131072]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 6), const(i32 : 30), const(i32 : 0), const(DeQParam : <6, 0.04755767>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 6), %6): // f16[1,256,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,256,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_118: f32[1,512,15,20]): // (f32[1,512,15,20]) -> u8[1,512,15,20]
  {
    %0 = GNNELoad(%var_118: f32[1,512,15,20]): // f16[1,512,15,20]
    %1 = GNNELoadW(const(f16[1,1,512,7])): // f16[1,1,512,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<6, 0.04755767>}), const(DeQParam : {<6, 0.04755767>}), const(i32 : 512), const(bool : False)): // u8[1,512,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,512,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_114: f16[1,128,15,20]): // (f16[1,128,15,20]) -> f32[1,128,15,20]
  {
    %0 = GNNELoad(%var_114: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<134, 0.070923>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // f16[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // f32[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_34: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_34: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[128,264,1,1])): // u8[128,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[32768])): // u8[32768]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 9), const(i32 : 29), const(i32 : 0), const(DeQParam : <9, 0.03289138>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 9), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_113: f16[1,256,15,20]): // (f16[1,256,15,20]) -> u8[1,256,15,20]
  {
    %0 = GNNELoad(%var_113: f16[1,256,15,20]): // f16[1,256,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<138, 0.096489966>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,256,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,256,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_33: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,256,15,20]
  {
    %0 = GNNELoad(%var_33: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[256,264,1,1])): // u8[256,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,256])): // u8[1,1,1,256]
    %3 = GNNELoadW(const(u8[256])): // u8[256]
    %4 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %5 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %6 = GNNELoadW(const(u8[65536])): // u8[65536]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 15), const(i32 : 28), const(i32 : 0), const(DeQParam : <15, 0.037573613>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 15), %6): // f16[1,256,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,256,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_177: u8[1,128,15,20], %var_178: u8[1,128,15,20]): // (u8[1,128,15,20], u8[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_177: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNELoad(%var_178: u8[1,128,15,20]): // u8[1,128,15,20]
    %2 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<27, 0.010383021>}), const(DeQParam : {<7, 0.037405983>}), const(i32 : 128), const(bool : False)): // u8[1,128,15,20]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_109: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_109: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<142, 0.047847778>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_29: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_29: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[128,264,1,1])): // u8[128,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[32768])): // u8[32768]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 30), const(i32 : 0), const(DeQParam : <12, 0.022767225>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_108: f16[1,256,15,20]): // (f16[1,256,15,20]) -> u8[1,256,15,20]
  {
    %0 = GNNELoad(%var_108: f16[1,256,15,20]): // f16[1,256,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<133, 0.06254839>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,256,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,256,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_28: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,256,15,20]
  {
    %0 = GNNELoad(%var_28: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[256,128,3,3])): // u8[256,128,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,256])): // u8[1,1,1,256]
    %3 = GNNELoadW(const(u8[256])): // u8[256]
    %4 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %5 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %6 = GNNELoadW(const(u8[294912])): // u8[294912]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 30), const(i32 : 0), const(DeQParam : <12, 0.023318172>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {2L,2L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,256,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,256,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_107: f16[1,128,30,40]): // (f16[1,128,30,40]) -> u8[1,128,30,40]
  {
    %0 = GNNELoad(%var_107: f16[1,128,30,40]): // f16[1,128,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<142, 0.067312665>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_27: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,128,30,40]
  {
    %0 = GNNELoad(%var_27: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[128,128,1,1])): // u8[128,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[16384])): // u8[16384]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 22), const(i32 : 28), const(i32 : 0), const(DeQParam : <22, 0.050959382>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 22), %6): // f16[1,128,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_175: u8[1,64,30,40], %var_176: u8[1,64,30,40]): // (u8[1,64,30,40], u8[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_175: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoad(%var_176: u8[1,64,30,40]): // u8[1,64,30,40]
    %2 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<29, 0.028560119>}), const(DeQParam : {<10, 0.026820064>}), const(i32 : 64), const(bool : False)): // u8[1,64,30,40]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_173: u8[1,64,30,40], %var_174: u8[1,64,30,40]): // (u8[1,64,30,40], u8[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_173: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoad(%var_174: u8[1,64,30,40]): // u8[1,64,30,40]
    %2 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<36, 0.015438139>}), const(DeQParam : {<14, 0.019772988>}), const(i32 : 64), const(bool : False)): // u8[1,64,30,40]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_171: u8[1,64,30,40], %var_172: u8[1,64,30,40]): // (u8[1,64,30,40], u8[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_171: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoad(%var_172: u8[1,64,30,40]): // u8[1,64,30,40]
    %2 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<29, 0.009539109>}), const(DeQParam : {<24, 0.011515381>}), const(i32 : 64), const(bool : False)): // u8[1,64,30,40]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_99: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_99: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<171, 0.0341616>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_19: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_19: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[64,128,1,1])): // u8[64,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[8192])): // u8[8192]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 16), const(i32 : 30), const(i32 : 0), const(DeQParam : <16, 0.0173718>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 16), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_98: f16[1,128,30,40]): // (f16[1,128,30,40]) -> u8[1,128,30,40]
  {
    %0 = GNNELoad(%var_98: f16[1,128,30,40]): // f16[1,128,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<150, 0.06007473>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_18: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,128,30,40]
  {
    %0 = GNNELoad(%var_18: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[128,64,3,3])): // u8[128,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[73728])): // u8[73728]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 15), const(i32 : 29), const(i32 : 0), const(DeQParam : <15, 0.018024918>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {2L,2L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 15), %6): // f16[1,128,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_97: f16[1,64,60,80]): // (f16[1,64,60,80]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_97: f16[1,64,60,80]): // f16[1,64,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<152, 0.057099365>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_17: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,64,60,80]
  {
    %0 = GNNELoad(%var_17: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 21), const(i32 : 28), const(i32 : 0), const(DeQParam : <21, 0.03944734>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 21), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_169: u8[1,32,60,80], %var_170: u8[1,32,60,80]): // (u8[1,32,60,80], u8[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_169: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoad(%var_170: u8[1,32,60,80]): // u8[1,32,60,80]
    %2 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<27, 0.02084186>}), const(DeQParam : {<11, 0.024735013>}), const(i32 : 32), const(bool : False)): // u8[1,32,60,80]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_167: u8[1,32,60,80], %var_168: u8[1,32,60,80]): // (u8[1,32,60,80], u8[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_167: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoad(%var_168: u8[1,32,60,80]): // u8[1,32,60,80]
    %2 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<21, 0.013251353>}), const(DeQParam : {<20, 0.0138387475>}), const(i32 : 32), const(bool : False)): // u8[1,32,60,80]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_91: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_91: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<186, 0.053617734>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_11: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_11: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[32,64,1,1])): // u8[32,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[2048])): // u8[2048]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 11), const(i32 : 29), const(i32 : 0), const(DeQParam : <11, 0.026261792>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 11), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_90: f16[1,64,60,80]): // (f16[1,64,60,80]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_90: f16[1,64,60,80]): // f16[1,64,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<140, 0.07058716>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_10: u8[1,32,120,160]): // (u8[1,32,120,160]) -> f16[1,64,60,80]
  {
    %0 = GNNELoad(%var_10: u8[1,32,120,160]): // u8[1,32,120,160]
    %1 = GNNELoadW(const(u8[64,32,3,3])): // u8[64,32,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[18432])): // u8[18432]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 5), const(i32 : 28), const(i32 : 0), const(DeQParam : <5, 0.05117206>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {2L,2L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 5), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_89: f16[1,32,120,160]): // (f16[1,32,120,160]) -> u8[1,32,120,160]
  {
    %0 = GNNELoad(%var_89: f16[1,32,120,160]): // f16[1,32,120,160]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<152, 0.16652897>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,120,160]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,120,160]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_9: u8[1,32,120,160]): // (u8[1,32,120,160]) -> f16[1,32,120,160]
  {
    %0 = GNNELoad(%var_9: u8[1,32,120,160]): // u8[1,32,120,160]
    %1 = GNNELoadW(const(u8[32,32,1,1])): // u8[32,32,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[1024])): // u8[1024]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 5), const(i32 : 26), const(i32 : 0), const(DeQParam : <5, 0.104858264>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 5), %6): // f16[1,32,120,160]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,120,160]
  }
  %TileAct1Case_0 = fusion<k230>(%var_165: u8[1,16,120,160], %var_166: u8[1,16,120,160]): // (u8[1,16,120,160], u8[1,16,120,160]) -> u8[1,16,120,160]
  {
    %0 = GNNELoad(%var_165: u8[1,16,120,160]): // u8[1,16,120,160]
    %1 = GNNELoad(%var_166: u8[1,16,120,160]): // u8[1,16,120,160]
    %2 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<8, 0.033033174>}), const(DeQParam : {<4, 0.06987228>}), const(i32 : 16), const(bool : False)): // u8[1,16,120,160]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // u8[1,16,120,160]
  }
  %TileAct1Case_0 = fusion<k230>(%var_85: f16[1,16,120,160]): // (f16[1,16,120,160]) -> u8[1,16,120,160]
  {
    %0 = GNNELoad(%var_85: f16[1,16,120,160]): // f16[1,16,120,160]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<192, 0.17828372>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,16,120,160]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,16,120,160]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_5: u8[1,32,120,160]): // (u8[1,32,120,160]) -> f16[1,16,120,160]
  {
    %0 = GNNELoad(%var_5: u8[1,32,120,160]): // u8[1,32,120,160]
    %1 = GNNELoadW(const(u8[16,32,1,1])): // u8[16,32,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,16])): // u8[1,1,1,16]
    %3 = GNNELoadW(const(u8[16])): // u8[16]
    %4 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %5 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %6 = GNNELoadW(const(u8[512])): // u8[512]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 1), const(i32 : 26), const(i32 : 0), const(DeQParam : <1, 0.28212526>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 1), %6): // f16[1,16,120,160]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,16,120,160]
  }
  %TileAct1Case_0 = fusion<k230>(%var_84: f16[1,32,120,160]): // (f16[1,32,120,160]) -> u8[1,32,120,160]
  {
    %0 = GNNELoad(%var_84: f16[1,32,120,160]): // f16[1,32,120,160]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<136, 0.71900636>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,120,160]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,120,160]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_4: u8[1,16,240,320]): // (u8[1,16,240,320]) -> f16[1,32,120,160]
  {
    %0 = GNNELoad(%var_4: u8[1,16,240,320]): // u8[1,16,240,320]
    %1 = GNNELoadW(const(u8[32,16,3,3])): // u8[32,16,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[4608])): // u8[4608]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 3), const(i32 : 25), const(i32 : 0), const(DeQParam : <3, 0.097572245>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {2L,2L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 3), %6): // f16[1,32,120,160]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,120,160]
  }
  %TileAct1Case_0 = fusion<k230>(%var_83: f16[1,16,240,320]): // (f16[1,16,240,320]) -> u8[1,16,240,320]
  {
    %0 = GNNELoad(%var_83: f16[1,16,240,320]): // f16[1,16,240,320]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<137, 0.23876056>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,16,240,320]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,16,240,320]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_3: u8[1,3,480,640]): // (u8[1,3,480,640]) -> f16[1,16,240,320]
  {
    %0 = GNNELoad(%var_3: u8[1,3,480,640]): // u8[1,3,480,640]
    %1 = GNNELoadW(const(u8[16,3,6,6])): // u8[16,3,6,6]
    %2 = GNNELoadW(const(u8[1,1,1,16])): // u8[1,1,1,16]
    %3 = GNNELoadW(const(u8[16])): // u8[16]
    %4 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %5 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %6 = GNNELoadW(const(u8[1728])): // u8[1728]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 0), const(i32 : 27), const(i32 : 0), const(DeQParam : <0, 0.0039220476>), const(i64[2,2] : {{2L,2L},{2L,2L}}), const(i64[2] : {2L,2L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 0), %6): // f16[1,16,240,320]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,16,240,320]
  }
  %0 = %TileConv2dCase_0(%var_2): // f16[1,16,240,320]
  %1 = %TileAct1Case_0(%0): // u8[1,16,240,320]
  %2 = %TileConv2dCase_0(%1): // f16[1,32,120,160]
  %3 = %TileAct1Case_0(%2): // u8[1,32,120,160]
  %4 = %TileConv2dCase_0(%3): // f16[1,16,120,160]
  %5 = %TileAct1Case_0(%4): // u8[1,16,120,160]
  %TileAct1Case_0 = fusion<k230>(%var_87: f16[1,16,120,160]): // (f16[1,16,120,160]) -> u8[1,16,120,160]
  {
    %0 = GNNELoad(%var_87: f16[1,16,120,160]): // f16[1,16,120,160]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<131, 0.18373618>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,16,120,160]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,16,120,160]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_7: u8[1,16,120,160]): // (u8[1,16,120,160]) -> f16[1,16,120,160]
  {
    %0 = GNNELoad(%var_7: u8[1,16,120,160]): // u8[1,16,120,160]
    %1 = GNNELoadW(const(u8[16,16,3,3])): // u8[16,16,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,16])): // u8[1,1,1,16]
    %3 = GNNELoadW(const(u8[16])): // u8[16]
    %4 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %5 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %6 = GNNELoadW(const(u8[2304])): // u8[2304]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 7), const(i32 : 27), const(i32 : 0), const(DeQParam : <7, 0.038826417>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 7), %6): // f16[1,16,120,160]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,16,120,160]
  }
  %TileAct1Case_0 = fusion<k230>(%var_86: f16[1,16,120,160]): // (f16[1,16,120,160]) -> u8[1,16,120,160]
  {
    %0 = GNNELoad(%var_86: f16[1,16,120,160]): // f16[1,16,120,160]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<172, 0.14611813>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,16,120,160]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,16,120,160]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_6: u8[1,16,120,160]): // (u8[1,16,120,160]) -> f16[1,16,120,160]
  {
    %0 = GNNELoad(%var_6: u8[1,16,120,160]): // u8[1,16,120,160]
    %1 = GNNELoadW(const(u8[16,16,1,1])): // u8[16,16,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,16])): // u8[1,1,1,16]
    %3 = GNNELoadW(const(u8[16])): // u8[16]
    %4 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %5 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %6 = GNNELoadW(const(u8[256])): // u8[256]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 8), const(i32 : 25), const(i32 : 0), const(DeQParam : <8, 0.033033174>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 8), %6): // f16[1,16,120,160]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,16,120,160]
  }
  %6 = %TileConv2dCase_0(%5): // f16[1,16,120,160]
  %7 = %TileAct1Case_0(%6): // u8[1,16,120,160]
  %8 = %TileConv2dCase_0(%7): // f16[1,16,120,160]
  %9 = %TileAct1Case_0(%8): // u8[1,16,120,160]
  %10 = %TileAct1Case_0(%5, %9): // u8[1,16,120,160]
  %TileAct1Case_0 = fusion<k230>(%var_88: f16[1,16,120,160]): // (f16[1,16,120,160]) -> u8[1,16,120,160]
  {
    %0 = GNNELoad(%var_88: f16[1,16,120,160]): // f16[1,16,120,160]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<147, 0.2819727>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,16,120,160]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,16,120,160]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_8: u8[1,32,120,160]): // (u8[1,32,120,160]) -> f16[1,16,120,160]
  {
    %0 = GNNELoad(%var_8: u8[1,32,120,160]): // u8[1,32,120,160]
    %1 = GNNELoadW(const(u8[16,32,1,1])): // u8[16,32,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,16])): // u8[1,1,1,16]
    %3 = GNNELoadW(const(u8[16])): // u8[16]
    %4 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %5 = GNNELoadW(const(f16[1,1,16,7])): // f16[1,1,16,7]
    %6 = GNNELoadW(const(u8[512])): // u8[512]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 1), const(i32 : 26), const(i32 : 0), const(DeQParam : <1, 0.28212526>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 1), %6): // f16[1,16,120,160]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,16,120,160]
  }
  %11 = %TileConv2dCase_0(%3): // f16[1,16,120,160]
  %12 = %TileAct1Case_0(%11): // u8[1,16,120,160]
  %13 = (%10, %12): // (u8[1,16,120,160], u8[1,16,120,160])
  
  %14 = Concat(Axis: 1, %13): // u8[1,32,120,160]
  %15 = %TileConv2dCase_0(%14): // f16[1,32,120,160]
  %16 = %TileAct1Case_0(%15): // u8[1,32,120,160]
  %17 = %TileConv2dCase_0(%16): // f16[1,64,60,80]
  %18 = %TileAct1Case_0(%17): // u8[1,64,60,80]
  %19 = %TileConv2dCase_0(%18): // f16[1,32,60,80]
  %20 = %TileAct1Case_0(%19): // u8[1,32,60,80]
  %TileAct1Case_0 = fusion<k230>(%var_93: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_93: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<157, 0.04318199>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_13: u8[1,32,60,80]): // (u8[1,32,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_13: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoadW(const(u8[32,32,3,3])): // u8[32,32,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[9216])): // u8[9216]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 13), const(i32 : 29), const(i32 : 0), const(DeQParam : <13, 0.020853441>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 13), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_92: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_92: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<149, 0.059077006>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_12: u8[1,32,60,80]): // (u8[1,32,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_12: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoadW(const(u8[32,32,1,1])): // u8[32,32,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[1024])): // u8[1024]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 21), const(i32 : 28), const(i32 : 0), const(DeQParam : <21, 0.013251353>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 21), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %21 = %TileConv2dCase_0(%20): // f16[1,32,60,80]
  %22 = %TileAct1Case_0(%21): // u8[1,32,60,80]
  %23 = %TileConv2dCase_0(%22): // f16[1,32,60,80]
  %24 = %TileAct1Case_0(%23): // u8[1,32,60,80]
  %25 = %TileAct1Case_0(%20, %24): // u8[1,32,60,80]
  %TileAct1Case_0 = fusion<k230>(%var_95: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_95: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<138, 0.06250312>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_15: u8[1,32,60,80]): // (u8[1,32,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_15: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoadW(const(u8[32,32,3,3])): // u8[32,32,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[9216])): // u8[9216]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 17), const(i32 : 29), const(i32 : 0), const(DeQParam : <17, 0.016235743>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 17), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_94: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_94: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<152, 0.050908104>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_14: u8[1,32,60,80]): // (u8[1,32,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_14: u8[1,32,60,80]): // u8[1,32,60,80]
    %1 = GNNELoadW(const(u8[32,32,1,1])): // u8[32,32,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[1024])): // u8[1024]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 27), const(i32 : 28), const(i32 : 0), const(DeQParam : <27, 0.02084186>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 27), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %26 = %TileConv2dCase_0(%25): // f16[1,32,60,80]
  %27 = %TileAct1Case_0(%26): // u8[1,32,60,80]
  %28 = %TileConv2dCase_0(%27): // f16[1,32,60,80]
  %29 = %TileAct1Case_0(%28): // u8[1,32,60,80]
  %30 = %TileAct1Case_0(%25, %29): // u8[1,32,60,80]
  %TileAct1Case_0 = fusion<k230>(%var_96: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_96: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<146, 0.076001845>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_16: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_16: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[32,64,1,1])): // u8[32,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[2048])): // u8[2048]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 11), const(i32 : 27), const(i32 : 0), const(DeQParam : <11, 0.026261792>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 11), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %31 = %TileConv2dCase_0(%18): // f16[1,32,60,80]
  %32 = %TileAct1Case_0(%31): // u8[1,32,60,80]
  %33 = (%30, %32): // (u8[1,32,60,80], u8[1,32,60,80])
  
  %34 = Concat(Axis: 1, %33): // u8[1,64,60,80]
  %35 = %TileConv2dCase_0(%34): // f16[1,64,60,80]
  %36 = %TileAct1Case_0(%35): // u8[1,64,60,80]
  %37 = %TileConv2dCase_0(%36): // f16[1,128,30,40]
  %38 = %TileAct1Case_0(%37): // u8[1,128,30,40]
  %39 = %TileConv2dCase_0(%38): // f16[1,64,30,40]
  %40 = %TileAct1Case_0(%39): // u8[1,64,30,40]
  %TileAct1Case_0 = fusion<k230>(%var_101: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_101: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<156, 0.03720964>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_21: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_21: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 30), const(i32 : 0), const(DeQParam : <12, 0.023509657>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_100: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_100: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<146, 0.0752277>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_20: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_20: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 29), const(i32 : 28), const(i32 : 0), const(DeQParam : <29, 0.009539109>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 29), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %41 = %TileConv2dCase_0(%40): // f16[1,64,30,40]
  %42 = %TileAct1Case_0(%41): // u8[1,64,30,40]
  %43 = %TileConv2dCase_0(%42): // f16[1,64,30,40]
  %44 = %TileAct1Case_0(%43): // u8[1,64,30,40]
  %45 = %TileAct1Case_0(%40, %44): // u8[1,64,30,40]
  %TileAct1Case_0 = fusion<k230>(%var_103: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_103: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<125, 0.05352779>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_23: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_23: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 16), const(i32 : 29), const(i32 : 0), const(DeQParam : <16, 0.017869348>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 16), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_102: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_102: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<151, 0.05841083>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_22: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_22: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 36), const(i32 : 28), const(i32 : 0), const(DeQParam : <36, 0.015438139>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 36), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %46 = %TileConv2dCase_0(%45): // f16[1,64,30,40]
  %47 = %TileAct1Case_0(%46): // u8[1,64,30,40]
  %48 = %TileConv2dCase_0(%47): // f16[1,64,30,40]
  %49 = %TileAct1Case_0(%48): // u8[1,64,30,40]
  %50 = %TileAct1Case_0(%45, %49): // u8[1,64,30,40]
  %TileAct1Case_0 = fusion<k230>(%var_105: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_105: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<131, 0.07582591>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_25: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_25: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 16), const(i32 : 29), const(i32 : 0), const(DeQParam : <16, 0.016994756>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 16), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_104: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_104: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<142, 0.062192287>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_24: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_24: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 29), const(i32 : 28), const(i32 : 0), const(DeQParam : <29, 0.028560119>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 29), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %51 = %TileConv2dCase_0(%50): // f16[1,64,30,40]
  %52 = %TileAct1Case_0(%51): // u8[1,64,30,40]
  %53 = %TileConv2dCase_0(%52): // f16[1,64,30,40]
  %54 = %TileAct1Case_0(%53): // u8[1,64,30,40]
  %55 = %TileAct1Case_0(%50, %54): // u8[1,64,30,40]
  %TileAct1Case_0 = fusion<k230>(%var_106: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_106: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<131, 0.07988956>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_26: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_26: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[64,128,1,1])): // u8[64,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[8192])): // u8[8192]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 16), const(i32 : 28), const(i32 : 0), const(DeQParam : <16, 0.0173718>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 16), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %56 = %TileConv2dCase_0(%38): // f16[1,64,30,40]
  %57 = %TileAct1Case_0(%56): // u8[1,64,30,40]
  %58 = (%55, %57): // (u8[1,64,30,40], u8[1,64,30,40])
  
  %59 = Concat(Axis: 1, %58): // u8[1,128,30,40]
  %60 = %TileConv2dCase_0(%59): // f16[1,128,30,40]
  %61 = %TileAct1Case_0(%60): // u8[1,128,30,40]
  %62 = %TileConv2dCase_0(%61): // f16[1,256,15,20]
  %63 = %TileAct1Case_0(%62): // u8[1,256,15,20]
  %64 = %TileConv2dCase_0(%63): // f16[1,128,15,20]
  %65 = %TileAct1Case_0(%64): // u8[1,128,15,20]
  %TileAct1Case_0 = fusion<k230>(%var_111: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_111: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<129, 0.1033855>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_31: u8[1,128,15,20]): // (u8[1,128,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_31: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNELoadW(const(u8[128,128,3,3])): // u8[128,128,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[147456])): // u8[147456]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 8), const(i32 : 30), const(i32 : 0), const(DeQParam : <8, 0.035052996>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 8), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_110: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_110: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<138, 0.13269295>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_30: u8[1,128,15,20]): // (u8[1,128,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_30: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNELoadW(const(u8[128,144,1,1])): // u8[128,144,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[16384])): // u8[16384]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 27), const(i32 : 28), const(i32 : 0), const(DeQParam : <27, 0.010383021>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 27), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %66 = %TileConv2dCase_0(%65): // f16[1,128,15,20]
  %67 = %TileAct1Case_0(%66): // u8[1,128,15,20]
  %68 = %TileConv2dCase_0(%67): // f16[1,128,15,20]
  %69 = %TileAct1Case_0(%68): // u8[1,128,15,20]
  %70 = %TileAct1Case_0(%65, %69): // u8[1,128,15,20]
  %TileAct1Case_0 = fusion<k230>(%var_112: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_112: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<137, 0.08417761>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_32: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_32: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[128,264,1,1])): // u8[128,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[32768])): // u8[32768]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 29), const(i32 : 0), const(DeQParam : <12, 0.022767225>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %71 = %TileConv2dCase_0(%63): // f16[1,128,15,20]
  %72 = %TileAct1Case_0(%71): // u8[1,128,15,20]
  %73 = (%70, %72): // (u8[1,128,15,20], u8[1,128,15,20])
  
  %74 = Concat(Axis: 1, %73): // u8[1,256,15,20]
  %75 = %TileConv2dCase_0(%74): // f16[1,256,15,20]
  %76 = %TileAct1Case_0(%75): // u8[1,256,15,20]
  %77 = %TileConv2dCase_0(%76): // f16[1,128,15,20]
  %78 = %TileAct1Case_0(%77): // f32[1,128,15,20]
  %TilePdp1Case_0 = fusion<k230>(%var_80: u8[1,128,15,20]): // (u8[1,128,15,20]) -> f32[1,128,15,20]
  {
    %0 = GNNELoad(%var_80: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNEPdp1(%0, const(i32[2] : {5,5}), const(i32[2] : {1,1}), const(i32[2,2] : {{2,2},{2,2}}), const(QParam : {<6, 21.027102>}), const(DeQParam : {<6, 0.04755767>}), const(f32 : 0), const(i32 : 0), const(bool : False)): // f16[1,128,15,20]
    %2 = GNNEStore(%1, const(i64[4] : {1L,1L,1L,1L})): // f32[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_115: f32[1,128,15,20]): // (f32[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_115: f32[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<6, 0.04755767>}), const(DeQParam : {<6, 0.04755767>}), const(i32 : 128), const(bool : False)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %79 = %TileAct1Case_0(%78): // u8[1,128,15,20]
  %80 = %TilePdp1Case_0(%79): // f32[1,128,15,20]
  %TilePdp1Case_0 = fusion<k230>(%var_81: u8[1,128,15,20]): // (u8[1,128,15,20]) -> f32[1,128,15,20]
  {
    %0 = GNNELoad(%var_81: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNEPdp1(%0, const(i32[2] : {5,5}), const(i32[2] : {1,1}), const(i32[2,2] : {{2,2},{2,2}}), const(QParam : {<2, 22.124372>}), const(DeQParam : {<6, 0.04755767>}), const(f32 : 0), const(i32 : 0), const(bool : False)): // f16[1,128,15,20]
    %2 = GNNEStore(%1, const(i64[4] : {1L,1L,1L,1L})): // f32[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_116: f32[1,128,15,20]): // (f32[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_116: f32[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<6, 0.04755767>}), const(DeQParam : {<6, 0.04755767>}), const(i32 : 128), const(bool : False)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %81 = %TileAct1Case_0(%80): // u8[1,128,15,20]
  %82 = %TilePdp1Case_0(%81): // f32[1,128,15,20]
  %TilePdp1Case_0 = fusion<k230>(%var_82: u8[1,128,15,20]): // (u8[1,128,15,20]) -> f32[1,128,15,20]
  {
    %0 = GNNELoad(%var_82: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNEPdp1(%0, const(i32[2] : {5,5}), const(i32[2] : {1,1}), const(i32[2,2] : {{2,2},{2,2}}), const(QParam : {<0, 21.521303>}), const(DeQParam : {<2, 0.04519902>}), const(f32 : 0), const(i32 : 0), const(bool : False)): // f16[1,128,15,20]
    %2 = GNNEStore(%1, const(i64[4] : {1L,1L,1L,1L})): // f32[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_117: f32[1,128,15,20]): // (f32[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_117: f32[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<2, 0.04519902>}), const(DeQParam : {<2, 0.04519902>}), const(i32 : 128), const(bool : False)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %83 = %TileAct1Case_0(%82): // u8[1,128,15,20]
  %84 = %TilePdp1Case_0(%83): // f32[1,128,15,20]
  %85 = (%78, %80, %82, %84): // (f32[1,128,15,20], f32[1,128,15,20], f32[1,128,15,20], f32[1,128,15,20])
  
  %86 = Concat(Axis: 1, %85): // f32[1,512,15,20]
  %87 = %TileAct1Case_0(%86): // u8[1,512,15,20]
  %88 = %TileConv2dCase_0(%87): // f16[1,256,15,20]
  %89 = %TileAct1Case_0(%88): // u8[1,256,15,20]
  %90 = %TileConv2dCase_0(%89): // f16[1,128,15,20]
  %91 = %TileAct1Case_0(%90): // u8[1,128,15,20]
  %92 = %TileAct1Case_0(%91): // u8[1,128,30,40]
  %93 = (%92, %61): // (u8[1,128,30,40], u8[1,128,30,40])
  
  %94 = Concat(Axis: 1, %93): // u8[1,256,30,40]
  %95 = %TileConv2dCase_0(%94): // f16[1,64,30,40]
  %96 = %TileAct1Case_0(%95): // u8[1,64,30,40]
  %97 = %TileConv2dCase_0(%96): // f16[1,64,30,40]
  %98 = %TileAct1Case_0(%97): // u8[1,64,30,40]
  %99 = %TileConv2dCase_0(%98): // f16[1,64,30,40]
  %100 = %TileAct1Case_0(%99): // u8[1,64,30,40]
  %TileAct1Case_0 = fusion<k230>(%var_125: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_125: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<149, 0.050126467>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_40: u8[1,256,30,40]): // (u8[1,256,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_40: u8[1,256,30,40]): // u8[1,256,30,40]
    %1 = GNNELoadW(const(u8[64,256,1,1])): // u8[64,256,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[16384])): // u8[16384]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 29), const(i32 : 0), const(DeQParam : <12, 0.023318172>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %101 = %TileConv2dCase_0(%94): // f16[1,64,30,40]
  %102 = %TileAct1Case_0(%101): // u8[1,64,30,40]
  %103 = (%100, %102): // (u8[1,64,30,40], u8[1,64,30,40])
  
  %104 = Concat(Axis: 1, %103): // u8[1,128,30,40]
  %105 = %TileConv2dCase_0(%104): // f16[1,128,30,40]
  %106 = %TileAct1Case_0(%105): // u8[1,128,30,40]
  %107 = %TileConv2dCase_0(%106): // f16[1,64,30,40]
  %108 = %TileAct1Case_0(%107): // u8[1,64,30,40]
  %109 = %TileAct1Case_0(%108): // u8[1,64,60,80]
  %110 = (%109, %36): // (u8[1,64,60,80], u8[1,64,60,80])
  
  %111 = Concat(Axis: 1, %110): // u8[1,128,60,80]
  %112 = %TileConv2dCase_0(%111): // f16[1,32,60,80]
  %113 = %TileAct1Case_0(%112): // u8[1,32,60,80]
  %114 = %TileConv2dCase_0(%113): // f16[1,32,60,80]
  %115 = %TileAct1Case_0(%114): // u8[1,32,60,80]
  %116 = %TileConv2dCase_0(%115): // f16[1,32,60,80]
  %117 = %TileAct1Case_0(%116): // u8[1,32,60,80]
  %TileAct1Case_0 = fusion<k230>(%var_132: f16[1,32,60,80]): // (f16[1,32,60,80]) -> u8[1,32,60,80]
  {
    %0 = GNNELoad(%var_132: f16[1,32,60,80]): // f16[1,32,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<135, 0.0290016>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,32,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,32,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_46: u8[1,128,60,80]): // (u8[1,128,60,80]) -> f16[1,32,60,80]
  {
    %0 = GNNELoad(%var_46: u8[1,128,60,80]): // u8[1,128,60,80]
    %1 = GNNELoadW(const(u8[32,128,1,1])): // u8[32,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,32])): // u8[1,1,1,32]
    %3 = GNNELoadW(const(u8[32])): // u8[32]
    %4 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %5 = GNNELoadW(const(f16[1,1,32,7])): // f16[1,1,32,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 15), const(i32 : 29), const(i32 : 0), const(DeQParam : <15, 0.018024918>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 15), %6): // f16[1,32,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,32,60,80]
  }
  %118 = %TileConv2dCase_0(%111): // f16[1,32,60,80]
  %119 = %TileAct1Case_0(%118): // u8[1,32,60,80]
  %120 = (%117, %119): // (u8[1,32,60,80], u8[1,32,60,80])
  
  %121 = Concat(Axis: 1, %120): // u8[1,64,60,80]
  %122 = %TileConv2dCase_0(%121): // f16[1,64,60,80]
  %123 = %TileAct1Case_0(%122): // u8[1,64,60,80]
  %124 = %TileConv2dCase_0(%123): // f16[1,64,60,80]
  %125 = %TileAct1Case_0(%124): // u8[1,64,60,80]
  %126 = %TileConv2dCase_0(%125): // f16[1,64,60,80]
  %127 = %TileAct1Case_0(%126): // u8[1,64,60,80]
  %128 = %TileConv2dCase_0(%127): // f32[1,64,60,80]
  %TileConv2dCase_0 = fusion<k230>(%var_53: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f32[1,2,60,80]
  {
    %0 = GNNELoad(%var_53: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[2,64,1,1])): // u8[2,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,2] : {{{{194,196}}}})): // u8[1,1,1,2]
    %3 = GNNELoadW(const(u8[2] : {194,196})): // u8[2]
    %4 = GNNELoadW(const(f16[1,1,2,7])): // f16[1,1,2,7]
    %5 = GNNELoadW(const(f16[1,1,2,7])): // f16[1,1,2,7]
    %6 = GNNELoadW(const(u8[128])): // u8[128]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 10), const(i32 : 30), const(i32 : 0), const(DeQParam : <10, 0.027558964>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 10), %6): // f16[1,2,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f32[1,2,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_137: f16[1,64,60,80]): // (f16[1,64,60,80]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_137: f16[1,64,60,80]): // f16[1,64,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<118, 0.06757657>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_52: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,64,60,80]
  {
    %0 = GNNELoad(%var_52: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 14), const(i32 : 30), const(i32 : 0), const(DeQParam : <14, 0.019409427>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 14), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,60,80]
  }
  %TileAct1Case_0 = fusion<k230>(%var_136: f16[1,64,60,80]): // (f16[1,64,60,80]) -> u8[1,64,60,80]
  {
    %0 = GNNELoad(%var_136: f16[1,64,60,80]): // f16[1,64,60,80]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<126, 0.05803258>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,60,80]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,60,80]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_51: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,64,60,80]
  {
    %0 = GNNELoad(%var_51: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 21), const(i32 : 30), const(i32 : 0), const(DeQParam : <21, 0.013036354>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 21), %6): // f16[1,64,60,80]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,60,80]
  }
  %129 = %TileConv2dCase_0(%123): // f16[1,64,60,80]
  %130 = %TileAct1Case_0(%129): // u8[1,64,60,80]
  %131 = %TileConv2dCase_0(%130): // f16[1,64,60,80]
  %132 = %TileAct1Case_0(%131): // u8[1,64,60,80]
  %133 = %TileConv2dCase_0(%132): // f32[1,2,60,80]
  %134 = (%128, %133): // (f32[1,64,60,80], f32[1,2,60,80])
  
  %135 = Concat(Axis: 1, %134): // f32[1,66,60,80]
  %136 = Reshape(%135, const(i32[4] : {1,1,66,4800})): // f32[1,1,66,4800]
  %TileConv2dCase_0 = fusion<k230>(%var_62: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f32[1,64,30,40]
  {
    %0 = GNNELoad(%var_62: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 5), const(i32 : 28), const(i32 : 0), const(DeQParam : <5, 0.05285234>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 5), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f32[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_145: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_145: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<123, 0.13178541>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_61: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_61: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 28), const(i32 : 0), const(DeQParam : <12, 0.022645634>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_144: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_144: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<176, 0.09166949>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_60: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_60: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[64,128,3,3])): // u8[64,128,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[73728])): // u8[73728]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 17), const(i32 : 30), const(i32 : 0), const(DeQParam : <17, 0.016897589>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 17), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_143: f16[1,128,30,40]): // (f16[1,128,30,40]) -> u8[1,128,30,40]
  {
    %0 = GNNELoad(%var_143: f16[1,128,30,40]): // f16[1,128,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<148, 0.057471648>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_59: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,128,30,40]
  {
    %0 = GNNELoad(%var_59: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[128,128,1,1])): // u8[128,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[16384])): // u8[16384]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 15), const(i32 : 29), const(i32 : 0), const(DeQParam : <15, 0.018990744>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 15), %6): // f16[1,128,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_141: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_141: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<136, 0.054646287>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_57: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_57: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 13), const(i32 : 29), const(i32 : 0), const(DeQParam : <13, 0.020970333>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 13), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_140: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_140: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<134, 0.06131294>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_56: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_56: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,1,1])): // u8[64,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 11), const(i32 : 28), const(i32 : 0), const(DeQParam : <11, 0.02538677>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 11), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_139: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_139: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<119, 0.06577781>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_55: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_55: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[64,128,1,1])): // u8[64,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[8192])): // u8[8192]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 13), const(i32 : 28), const(i32 : 0), const(DeQParam : <13, 0.022053791>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 13), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_138: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_138: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<134, 0.059327405>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_54: u8[1,64,60,80]): // (u8[1,64,60,80]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_54: u8[1,64,60,80]): // u8[1,64,60,80]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 21), const(i32 : 30), const(i32 : 0), const(DeQParam : <21, 0.013036354>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {2L,2L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 21), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %137 = %TileConv2dCase_0(%123): // f16[1,64,30,40]
  %138 = %TileAct1Case_0(%137): // u8[1,64,30,40]
  %139 = (%138, %108): // (u8[1,64,30,40], u8[1,64,30,40])
  
  %140 = Concat(Axis: 1, %139): // u8[1,128,30,40]
  %141 = %TileConv2dCase_0(%140): // f16[1,64,30,40]
  %142 = %TileAct1Case_0(%141): // u8[1,64,30,40]
  %143 = %TileConv2dCase_0(%142): // f16[1,64,30,40]
  %144 = %TileAct1Case_0(%143): // u8[1,64,30,40]
  %145 = %TileConv2dCase_0(%144): // f16[1,64,30,40]
  %146 = %TileAct1Case_0(%145): // u8[1,64,30,40]
  %TileAct1Case_0 = fusion<k230>(%var_142: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_142: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<124, 0.038492493>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_58: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_58: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[64,128,1,1])): // u8[64,128,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[8192])): // u8[8192]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 13), const(i32 : 29), const(i32 : 0), const(DeQParam : <13, 0.022053791>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 13), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %147 = %TileConv2dCase_0(%140): // f16[1,64,30,40]
  %148 = %TileAct1Case_0(%147): // u8[1,64,30,40]
  %149 = (%146, %148): // (u8[1,64,30,40], u8[1,64,30,40])
  
  %150 = Concat(Axis: 1, %149): // u8[1,128,30,40]
  %151 = %TileConv2dCase_0(%150): // f16[1,128,30,40]
  %152 = %TileAct1Case_0(%151): // u8[1,128,30,40]
  %153 = %TileConv2dCase_0(%152): // f16[1,64,30,40]
  %154 = %TileAct1Case_0(%153): // u8[1,64,30,40]
  %155 = %TileConv2dCase_0(%154): // f16[1,64,30,40]
  %156 = %TileAct1Case_0(%155): // u8[1,64,30,40]
  %157 = %TileConv2dCase_0(%156): // f32[1,64,30,40]
  %TileConv2dCase_0 = fusion<k230>(%var_65: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f32[1,2,30,40]
  {
    %0 = GNNELoad(%var_65: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[2,64,1,1])): // u8[2,64,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,2] : {{{{200,198}}}})): // u8[1,1,1,2]
    %3 = GNNELoadW(const(u8[2] : {200,198})): // u8[2]
    %4 = GNNELoadW(const(f16[1,1,2,7])): // f16[1,1,2,7]
    %5 = GNNELoadW(const(f16[1,1,2,7])): // f16[1,1,2,7]
    %6 = GNNELoadW(const(u8[128])): // u8[128]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 8), const(i32 : 29), const(i32 : 0), const(DeQParam : <8, 0.032969315>), const(i64[2,2] : {{0L,0L},{0L,0L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 8), %6): // f16[1,2,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f32[1,2,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_147: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_147: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<116, 0.07195083>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_64: u8[1,64,30,40]): // (u8[1,64,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_64: u8[1,64,30,40]): // u8[1,64,30,40]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 11), const(i32 : 30), const(i32 : 0), const(DeQParam : <11, 0.026299216>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 11), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %TileAct1Case_0 = fusion<k230>(%var_146: f16[1,64,30,40]): // (f16[1,64,30,40]) -> u8[1,64,30,40]
  {
    %0 = GNNELoad(%var_146: f16[1,64,30,40]): // f16[1,64,30,40]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<133, 0.06953255>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,30,40]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,30,40]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_63: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,64,30,40]
  {
    %0 = GNNELoad(%var_63: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[64,128,3,3])): // u8[64,128,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[73728])): // u8[73728]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 17), const(i32 : 30), const(i32 : 0), const(DeQParam : <17, 0.016897589>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 17), %6): // f16[1,64,30,40]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,30,40]
  }
  %158 = %TileConv2dCase_0(%152): // f16[1,64,30,40]
  %159 = %TileAct1Case_0(%158): // u8[1,64,30,40]
  %160 = %TileConv2dCase_0(%159): // f16[1,64,30,40]
  %161 = %TileAct1Case_0(%160): // u8[1,64,30,40]
  %162 = %TileConv2dCase_0(%161): // f32[1,2,30,40]
  %163 = (%157, %162): // (f32[1,64,30,40], f32[1,2,30,40])
  
  %164 = Concat(Axis: 1, %163): // f32[1,66,30,40]
  %165 = Reshape(%164, const(i32[4] : {1,1,66,1200})): // f32[1,1,66,1200]
  %TileConv2dCase_0 = fusion<k230>(%var_74: u8[1,64,15,20]): // (u8[1,64,15,20]) -> f32[1,64,15,20]
  {
    %0 = GNNELoad(%var_74: u8[1,64,15,20]): // u8[1,64,15,20]
    %1 = GNNELoadW(const(u8[64,72,1,1])): // u8[64,72,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[4096])): // u8[4096]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 5), const(i32 : 28), const(i32 : 0), const(DeQParam : <5, 0.06098069>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 5), %6): // f16[1,64,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f32[1,64,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_155: f16[1,64,15,20]): // (f16[1,64,15,20]) -> u8[1,64,15,20]
  {
    %0 = GNNELoad(%var_155: f16[1,64,15,20]): // f16[1,64,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<107, 0.1326139>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_73: u8[1,64,15,20]): // (u8[1,64,15,20]) -> f16[1,64,15,20]
  {
    %0 = GNNELoad(%var_73: u8[1,64,15,20]): // u8[1,64,15,20]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 9), const(i32 : 28), const(i32 : 0), const(DeQParam : <9, 0.030481499>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 9), %6): // f16[1,64,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_154: f16[1,64,15,20]): // (f16[1,64,15,20]) -> u8[1,64,15,20]
  {
    %0 = GNNELoad(%var_154: f16[1,64,15,20]): // f16[1,64,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<158, 0.10393934>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_72: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,64,15,20]
  {
    %0 = GNNELoad(%var_72: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[64,256,3,3])): // u8[64,256,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[147456])): // u8[147456]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 11), const(i32 : 30), const(i32 : 0), const(DeQParam : <11, 0.024363508>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 11), %6): // f16[1,64,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_153: f16[1,256,15,20]): // (f16[1,256,15,20]) -> u8[1,256,15,20]
  {
    %0 = GNNELoad(%var_153: f16[1,256,15,20]): // f16[1,256,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<149, 0.08383376>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,256,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,256,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_71: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,256,15,20]
  {
    %0 = GNNELoad(%var_71: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[256,264,1,1])): // u8[256,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,256])): // u8[1,1,1,256]
    %3 = GNNELoadW(const(u8[256])): // u8[256]
    %4 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %5 = GNNELoadW(const(f16[1,1,256,7])): // f16[1,1,256,7]
    %6 = GNNELoadW(const(u8[65536])): // u8[65536]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 29), const(i32 : 0), const(DeQParam : <12, 0.023477348>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,256,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,256,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_151: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_151: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<123, 0.07288939>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_69: u8[1,128,15,20]): // (u8[1,128,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_69: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNELoadW(const(u8[128,128,3,3])): // u8[128,128,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[147456])): // u8[147456]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 9), const(i32 : 29), const(i32 : 0), const(DeQParam : <9, 0.029798703>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 9), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_150: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_150: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<127, 0.08173097>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_68: u8[1,128,15,20]): // (u8[1,128,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_68: u8[1,128,15,20]): // u8[1,128,15,20]
    %1 = GNNELoadW(const(u8[128,144,1,1])): // u8[128,144,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[16384])): // u8[16384]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 9), const(i32 : 28), const(i32 : 0), const(DeQParam : <9, 0.029826565>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 9), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_149: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_149: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<131, 0.081451625>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_67: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_67: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[128,264,1,1])): // u8[128,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[32768])): // u8[32768]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 28), const(i32 : 0), const(DeQParam : <12, 0.023429077>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_148: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_148: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<124, 0.062472112>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_66: u8[1,128,30,40]): // (u8[1,128,30,40]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_66: u8[1,128,30,40]): // u8[1,128,30,40]
    %1 = GNNELoadW(const(u8[128,128,3,3])): // u8[128,128,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[147456])): // u8[147456]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 17), const(i32 : 30), const(i32 : 0), const(DeQParam : <17, 0.016897589>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {2L,2L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 17), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %166 = %TileConv2dCase_0(%152): // f16[1,128,15,20]
  %167 = %TileAct1Case_0(%166): // u8[1,128,15,20]
  %168 = (%167, %91): // (u8[1,128,15,20], u8[1,128,15,20])
  
  %169 = Concat(Axis: 1, %168): // u8[1,256,15,20]
  %170 = %TileConv2dCase_0(%169): // f16[1,128,15,20]
  %171 = %TileAct1Case_0(%170): // u8[1,128,15,20]
  %172 = %TileConv2dCase_0(%171): // f16[1,128,15,20]
  %173 = %TileAct1Case_0(%172): // u8[1,128,15,20]
  %174 = %TileConv2dCase_0(%173): // f16[1,128,15,20]
  %175 = %TileAct1Case_0(%174): // u8[1,128,15,20]
  %TileAct1Case_0 = fusion<k230>(%var_152: f16[1,128,15,20]): // (f16[1,128,15,20]) -> u8[1,128,15,20]
  {
    %0 = GNNELoad(%var_152: f16[1,128,15,20]): // f16[1,128,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<131, 0.069341846>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,128,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,128,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_70: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,128,15,20]
  {
    %0 = GNNELoad(%var_70: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[128,264,1,1])): // u8[128,264,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,128])): // u8[1,1,1,128]
    %3 = GNNELoadW(const(u8[128])): // u8[128]
    %4 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %5 = GNNELoadW(const(f16[1,1,128,7])): // f16[1,1,128,7]
    %6 = GNNELoadW(const(u8[32768])): // u8[32768]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 29), const(i32 : 0), const(DeQParam : <12, 0.023429077>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,128,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,128,15,20]
  }
  %176 = %TileConv2dCase_0(%169): // f16[1,128,15,20]
  %177 = %TileAct1Case_0(%176): // u8[1,128,15,20]
  %178 = (%175, %177): // (u8[1,128,15,20], u8[1,128,15,20])
  
  %179 = Concat(Axis: 1, %178): // u8[1,256,15,20]
  %180 = %TileConv2dCase_0(%179): // f16[1,256,15,20]
  %181 = %TileAct1Case_0(%180): // u8[1,256,15,20]
  %182 = %TileConv2dCase_0(%181): // f16[1,64,15,20]
  %183 = %TileAct1Case_0(%182): // u8[1,64,15,20]
  %184 = %TileConv2dCase_0(%183): // f16[1,64,15,20]
  %185 = %TileAct1Case_0(%184): // u8[1,64,15,20]
  %186 = %TileConv2dCase_0(%185): // f32[1,64,15,20]
  %TileConv2dCase_0 = fusion<k230>(%var_77: u8[1,64,15,20]): // (u8[1,64,15,20]) -> f32[1,2,15,20]
  {
    %0 = GNNELoad(%var_77: u8[1,64,15,20]): // u8[1,64,15,20]
    %1 = GNNELoadW(const(u8[2,72,1,1])): // u8[2,72,1,1]
    %2 = GNNELoadW(const(u8[1,1,1,2] : {{{{214,207}}}})): // u8[1,1,1,2]
    %3 = GNNELoadW(const(u8[2] : {214,207})): // u8[2]
    %4 = GNNELoadW(const(f16[1,1,2,7])): // f16[1,1,2,7]
    %5 = GNNELoadW(const(f16[1,1,2,7])): // f16[1,1,2,7]
    %6 = GNNELoadW(const(u8[128])): // u8[128]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 10), const(i32 : 30), const(i32 : 0), const(DeQParam : <10, 0.027106717>), const(i32[2,2] : {{0,0},{0,0}}), const(i32[2] : {1,1}), const(i32[2] : {1,1}), const(i32 : 1), const(bool : False), const(i16 : 10), %6): // f16[1,2,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f32[1,2,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_157: f16[1,64,15,20]): // (f16[1,64,15,20]) -> u8[1,64,15,20]
  {
    %0 = GNNELoad(%var_157: f16[1,64,15,20]): // f16[1,64,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<124, 0.074416004>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_76: u8[1,64,15,20]): // (u8[1,64,15,20]) -> f16[1,64,15,20]
  {
    %0 = GNNELoad(%var_76: u8[1,64,15,20]): // u8[1,64,15,20]
    %1 = GNNELoadW(const(u8[64,64,3,3])): // u8[64,64,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[36864])): // u8[36864]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 12), const(i32 : 30), const(i32 : 0), const(DeQParam : <12, 0.024020888>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 12), %6): // f16[1,64,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,15,20]
  }
  %TileAct1Case_0 = fusion<k230>(%var_156: f16[1,64,15,20]): // (f16[1,64,15,20]) -> u8[1,64,15,20]
  {
    %0 = GNNELoad(%var_156: f16[1,64,15,20]): // f16[1,64,15,20]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<106, 0.059086405>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // u8[1,64,15,20]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,64,15,20]
  }
  %TileConv2dCase_0 = fusion<k230>(%var_75: u8[1,256,15,20]): // (u8[1,256,15,20]) -> f16[1,64,15,20]
  {
    %0 = GNNELoad(%var_75: u8[1,256,15,20]): // u8[1,256,15,20]
    %1 = GNNELoadW(const(u8[64,256,3,3])): // u8[64,256,3,3]
    %2 = GNNELoadW(const(u8[1,1,1,64])): // u8[1,1,1,64]
    %3 = GNNELoadW(const(u8[64])): // u8[64]
    %4 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %5 = GNNELoadW(const(f16[1,1,64,7])): // f16[1,1,64,7]
    %6 = GNNELoadW(const(u8[147456])): // u8[147456]
    %7 = GNNEConv2D(%0, %1, %2, %3, %4, %5, const(i32 : 11), const(i32 : 30), const(i32 : 0), const(DeQParam : <11, 0.024363508>), const(i64[2,2] : {{1L,1L},{1L,1L}}), const(i64[2] : {1L,1L}), const(i64[2] : {1L,1L}), const(i32 : 1), const(bool : False), const(i16 : 11), %6): // f16[1,64,15,20]
    %8 = GNNEStore(%7, const(i64[4] : {1L,1L,1L,1L})): // f16[1,64,15,20]
  }
  %187 = %TileConv2dCase_0(%181): // f16[1,64,15,20]
  %188 = %TileAct1Case_0(%187): // u8[1,64,15,20]
  %189 = %TileConv2dCase_0(%188): // f16[1,64,15,20]
  %190 = %TileAct1Case_0(%189): // u8[1,64,15,20]
  %191 = %TileConv2dCase_0(%190): // f32[1,2,15,20]
  %192 = (%186, %191): // (f32[1,64,15,20], f32[1,2,15,20])
  
  %193 = Concat(Axis: 1, %192): // f32[1,66,15,20]
  %194 = Reshape(%193, const(i32[4] : {1,1,66,300})): // f32[1,1,66,300]
  %195 = (%136, %165, %194): // (f32[1,1,66,4800], f32[1,1,66,1200], f32[1,1,66,300])
  
  %196 = Concat(Axis: 3, %195): // f32[1,1,66,6300]
  %197 = Squeeze(%196, const(i32[1] : {0})): // f32[1,66,6300]
  %198 = Reshape(%197, const(i32[4] : {1,1,66,6300})): // f32[1,1,66,6300]
  %199 = Split(%198, const(i32 : 2), const(i32[2] : {64,2})): // (f32[1,1,64,6300], f32[1,1,2,6300])
  %200 = GetItem(%199, const(i32 : 0)): // f32[1,1,64,6300]
  %201 = Squeeze(%200, const(i32[1] : {0})): // f32[1,64,6300]
  %202 = GetItem(%199, const(i32 : 1)): // f32[1,1,2,6300]
  %203 = Squeeze(%202, const(i32[1] : {0})): // f32[1,2,6300]
  %204 = (%201, %203): // (f32[1,64,6300], f32[1,2,6300])
  
  %205 = GetItem(%204, const(i32 : 0)): // f32[1,64,6300]
  %206 = Reshape(%205, const(i64[4] : {1L,4L,16L,6300L})): // f32[1,4,16,6300]
  %207 = %TileTransposeCase_0(%206): // f32[1,16,4,6300]
  %208 = Softmax(%207, const(i64 : 1)): // f32[1,16,4,6300]
  %209 = %TileAct1Case_0(%208): // u8[1,16,4,6300]
  %210 = %TileConv2dCase_0(%209): // f32[1,1,4,6300]
  %211 = Reshape(%210, const(i64[3] : {1L,4L,6300L})): // f32[1,4,6300]
  %212 = Slice(%211, const(i64[1] : {0L}), const(i64[1] : {2L}), const(i64[1] : {1L}), const(i32[1] : {1})): // f32[1,2,6300]
  %213 = Binary(BinaryOp.Sub, const(f32[1,2,6300]), %212): // f32[1,2,6300]
  %214 = Reshape(%213, const(i32[4] : {1,1,2,6300})): // f32[1,1,2,6300]
  %215 = %TileAct1Case_0(%214): // u8[1,1,2,6300]
  %TileAct1Case_0 = fusion<k230>(%var_161: f32[1,1,2,6300]): // (f32[1,1,2,6300]) -> u8[1,1,2,6300]
  {
    %0 = GNNELoad(%var_161: f32[1,1,2,6300]): // f16[1,1,2,6300]
    %1 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)2.959,(Half)2.959,(Half)0,(Half)0,(Half)0,(Half)255}}}})): // f16[1,1,1,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<0, 0.33789083>}), const(DeQParam : {<0, 0.33789083>}), const(i32 : 1), const(bool : False)): // u8[1,1,2,6300]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,1,2,6300]
  }
  %TileAct1Case_0 = fusion<k230>(%var_179: u8[1,1,2,6300]): // (u8[1,1,2,6300]) -> f32[1,1,2,6300]
  {
    %0 = GNNELoad(%var_179: u8[1,1,2,6300]): // u8[1,1,2,6300]
    %1 = GNNELoad(const(f32[1,1,2,6300])): // f16[1,1,2,6300]
    %2 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)1,(Half)1,(Half)0,(Half)0,(Half)-Infinity,(Half)Infinity}}}})): // f16[1,1,1,7]
    %3 = GNNEActivation(%0, %1, %2, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<0, 0.047859866>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : False)): // f16[1,1,2,6300]
    %4 = GNNEStore(%3, const(i64[4] : {1L,1L,1L,1L})): // f32[1,1,2,6300]
  }
  %TileAct1Case_0 = fusion<k230>(%var_160: f32[1,1,2,6300]): // (f32[1,1,2,6300]) -> u8[1,1,2,6300]
  {
    %0 = GNNELoad(%var_160: f32[1,1,2,6300]): // f16[1,1,2,6300]
    %1 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)20.89,(Half)20.89,(Half)0,(Half)0,(Half)0,(Half)255}}}})): // f16[1,1,1,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<0, 0.047859866>}), const(DeQParam : {<0, 0.047859866>}), const(i32 : 1), const(bool : False)): // u8[1,1,2,6300]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,1,2,6300]
  }
  %216 = Slice(%211, const(i64[1] : {2L}), const(i64[1] : {4L}), const(i64[1] : {1L}), const(i32[1] : {1})): // f32[1,2,6300]
  %217 = Reshape(%216, const(i32[4] : {1,1,2,6300})): // f32[1,1,2,6300]
  %218 = %TileAct1Case_0(%217): // u8[1,1,2,6300]
  %219 = %TileAct1Case_0(%218): // f32[1,1,2,6300]
  %220 = %TileAct1Case_0(%219): // u8[1,1,2,6300]
  %221 = %TileAct1Case_0(%215, %220): // u8[1,1,2,6300]
  %222 = Reshape(%221, const(i32[3] : {1,2,6300})): // u8[1,2,6300]
  %TileAct1Case_0 = fusion<k230>(%var_162: f32[1,1,2,6300]): // (f32[1,1,2,6300]) -> u8[1,1,2,6300]
  {
    %0 = GNNELoad(%var_162: f32[1,1,2,6300]): // f16[1,1,2,6300]
    %1 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)3.207,(Half)3.207,(Half)0,(Half)0,(Half)0,(Half)255}}}})): // f16[1,1,1,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<0, 0.3118054>}), const(DeQParam : {<0, 0.3118054>}), const(i32 : 1), const(bool : False)): // u8[1,1,2,6300]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,1,2,6300]
  }
  %223 = Reshape(%219, const(i32[3] : {1,2,6300})): // f32[1,2,6300]
  %224 = Binary(BinaryOp.Sub, %223, %213): // f32[1,2,6300]
  %225 = Reshape(%224, const(i32[4] : {1,1,2,6300})): // f32[1,1,2,6300]
  %226 = %TileAct1Case_0(%225): // u8[1,1,2,6300]
  %227 = Reshape(%226, const(i32[3] : {1,2,6300})): // u8[1,2,6300]
  %228 = (%222, %227): // (u8[1,2,6300], u8[1,2,6300])
  
  %229 = Concat(Axis: 1, %228): // u8[1,4,6300]
  %230 = Reshape(%229, const(i32[4] : {1,1,4,6300})): // u8[1,1,4,6300]
  %231 = %TileAct1Case_0(%230): // f32[1,1,4,6300]
  %232 = Reshape(%231, const(i32[4] : {1,1,4,6300})): // f32[1,1,4,6300]
  %TileAct1Case_0 = fusion<k230>(%var_164: u8[1,1,2,6300]): // (u8[1,1,2,6300]) -> f32[1,1,2,6300]
  {
    %0 = GNNELoad(%var_164: u8[1,1,2,6300]): // u8[1,1,2,6300]
    %1 = GNNELoadW(const(f16[1,1,1,49])): // f16[1,1,1,49]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<249, 0.13005945>}), const(DeQParam : {<0, 1>}), const(i32 : 1), const(bool : True)): // f16[1,1,2,6300]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // f32[1,1,2,6300]
  }
  %TileAct1Case_0 = fusion<k230>(%var_163: f32[1,1,2,6300]): // (f32[1,1,2,6300]) -> u8[1,1,2,6300]
  {
    %0 = GNNELoad(%var_163: f32[1,1,2,6300]): // f16[1,1,2,6300]
    %1 = GNNELoadW(const(f16[1,1,1,7] : {{{{(Half)0,(Half)7.688,(Half)7.688,(Half)249,(Half)249,(Half)0,(Half)255}}}})): // f16[1,1,1,7]
    %2 = GNNEActivation(%0, None, %1, const(i32 : 0), const(i32 : 0), const(i32 : 0), const(DeQParam : {<249, 0.13005945>}), const(DeQParam : {<249, 0.13005945>}), const(i32 : 1), const(bool : False)): // u8[1,1,2,6300]
    %3 = GNNEStore(%2, const(i64[4] : {1L,1L,1L,1L})): // u8[1,1,2,6300]
  }
  %233 = GetItem(%204, const(i32 : 1)): // f32[1,2,6300]
  %234 = Reshape(%233, const(i32[4] : {1,1,2,6300})): // f32[1,1,2,6300]
  %235 = %TileAct1Case_0(%234): // u8[1,1,2,6300]
  %236 = %TileAct1Case_0(%235): // f32[1,1,2,6300]
  %237 = Reshape(%236, const(i32[4] : {1,1,2,6300})): // f32[1,1,2,6300]
  %238 = (%232, %237): // (f32[1,1,4,6300], f32[1,1,2,6300])
  
  %239 = Concat(Axis: 2, %238): // f32[1,1,6,6300]
  %240 = Squeeze(%239, const(i32[1] : {0})): // f32[1,6,6300]
}
