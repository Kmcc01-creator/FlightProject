// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#include "IoUring/FlightIoUringTypes.h"

#if PLATFORM_LINUX

namespace Flight::IoUring
{

// ============================================================================
// FSqe Prep Methods
// ============================================================================

void FSqe::PrepRead(int32 InFd, void* Buf, uint32 Size, uint64 Offset, uint64 InUserData)
{
	Zero();
	Opcode = static_cast<uint8>(EOpcode::Read);
	Fd = InFd;
	AddrOrSpliceOff = reinterpret_cast<uint64>(Buf);
	Len = Size;
	OffOrAddr2 = Offset;
	UserData = InUserData;
}

void FSqe::PrepWrite(int32 InFd, const void* Buf, uint32 Size, uint64 Offset, uint64 InUserData)
{
	Zero();
	Opcode = static_cast<uint8>(EOpcode::Write);
	Fd = InFd;
	AddrOrSpliceOff = reinterpret_cast<uint64>(Buf);
	Len = Size;
	OffOrAddr2 = Offset;
	UserData = InUserData;
}

void FSqe::PrepClose(int32 InFd, uint64 InUserData)
{
	Zero();
	Opcode = static_cast<uint8>(EOpcode::Close);
	Fd = InFd;
	UserData = InUserData;
}

void FSqe::PrepPollAdd(int32 InFd, uint32 PollMask, uint64 InUserData)
{
	Zero();
	Opcode = static_cast<uint8>(EOpcode::PollAdd);
	Fd = InFd;
	// Poll mask goes in the union field (low 32 bits of OpFlags for poll events)
	OpFlags = PollMask;
	UserData = InUserData;
}

void FSqe::PrepPollMultishot(int32 InFd, uint32 PollMask, uint64 InUserData)
{
	PrepPollAdd(InFd, PollMask, InUserData);
	// Set multishot flag in len field (IORING_POLL_ADD_MULTI)
	Len = PollFlags::AddMulti;
}

void FSqe::PrepMsgRing(int32 TargetFd, uint64 Payload, uint32 MsgTag, uint64 InUserData)
{
	Zero();
	Opcode = static_cast<uint8>(EOpcode::MsgRing);
	Fd = TargetFd;
	// For MSG_RING:
	// - len field = result value to post
	// - off field = user_data to post on target
	// - addr = flags (MSG_DATA=0, MSG_SEND_FD=1)
	Len = MsgTag;
	OffOrAddr2 = Payload;
	AddrOrSpliceOff = MsgRing::MsgData;
	UserData = InUserData;
}

void FSqe::PrepMsgRingWithFlags(int32 TargetFd, uint64 Payload, uint32 MsgTag,
                                 uint32 CqeFlags, uint64 InUserData)
{
	PrepMsgRing(TargetFd, Payload, MsgTag, InUserData);
	// Set FLAGS_PASS to forward CQE flags to target
	AddrOrSpliceOff |= MsgRing::FlagsPass;
	// SQE flags field carries the CQE flags to pass through
	// Actually, for MSG_RING with FLAGS_PASS, the CQE flags go in file_index
	SpliceFdOrFileIndex = static_cast<int32>(CqeFlags);
}

void FSqe::PrepNop(uint64 InUserData)
{
	Zero();
	Opcode = static_cast<uint8>(EOpcode::Nop);
	UserData = InUserData;
}

void FSqe::PrepFsync(int32 InFd, uint32 FsyncFlags, uint64 InUserData)
{
	Zero();
	Opcode = static_cast<uint8>(EOpcode::Fsync);
	Fd = InFd;
	OpFlags = FsyncFlags;
	UserData = InUserData;
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
