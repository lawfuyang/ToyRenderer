#pragma once

struct Symbol
{
	uint64_t Address;
	char Name[255];
	char FilePath[MAX_PATH];
	uint32_t LineNumber;
};

namespace StackTrace
{
	uint32_t Trace(void** pStackData, uint32_t stackSize, uint32_t skipDepth);
	void Resolve(std::span<uint64_t> stackFrame, uint32_t numFrames, Symbol* outSymbols);
}


template<uint32_t Size>
class Callstack
{
public:
	void Trace(uint32_t skipDepth = 0)
	{
		m_Resolved = false;
		m_NumFrames = StackTrace::Trace((void**)&m_Stack, Size, skipDepth + 1);
	}

	void Resolve()
	{
		if (!m_Resolved)
		{
			StackTrace::Resolve(std::span{ m_Stack }, m_NumFrames, m_Symbols);
			m_Resolved = true;
		}
	}

	std::string ToString()
	{
		Resolve();
		std::string output;
		for (uint32_t i = 0; i < m_NumFrames; i++)
		{
			const Symbol& s = m_Symbols[i];
			output += StringFormat("\t0x%x - %s() - Line %d\n", s.Address, s.Name, s.LineNumber);
		}
		return output;
	}

private:
	bool m_Resolved = false;
	uint32_t m_NumFrames = 0;
	Symbol m_Symbols[Size];
	uint64_t m_Stack[Size];
};
