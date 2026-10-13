#pragma once
struct TestBootstrap { void shutdown() {} };
extern TestBootstrap testBootstrap;
#define CH585_ROLE_BOOTSTRAP testBootstrap
