exports.capability = async function capability() {
  const result = new DataView(new ArrayBuffer(32));
  result.setUint32(0, 0x32485758, true);
  result.setUint16(4, 2, true); result.setUint16(6, 1024, true);
  result.setUint8(8, 2); result.setUint8(9, 1); result.setUint8(11, 8);
  result.setUint32(12, 15000000, true); result.setUint32(16, 1, true);
  return result;
};
