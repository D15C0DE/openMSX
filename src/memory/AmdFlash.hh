#ifndef AMDFLASH_HH
#define AMDFLASH_HH

#include "MemBuffer.hh"
#include "openmsx.hh"
#include "noncopyable.hh"
#include <memory>
#include <vector>

namespace openmsx {

class MSXMotherBoard;
class Rom;
class SRAM;
class DeviceConfig;

class AmdFlash : private noncopyable
{
public:
	struct SectorInfo {
		unsigned size;
		bool writeProtected;
	};
	/** Create AmdFlash with given configuration.
	 * @param rom The initial content for this flash
	 * @param sectorInfo
	 *   A vector containing the size and write protected status of each
	 *   sector in the flash. This implicitly also communicates the number
	 *   of sectors (a sector is a region in the flash that can be erased
	 *   individually). There exist flash roms were the different sectors
	 *   are not all equally large, that's why it's required to enumerate
	 *   the size of each sector (instead of simply specifying the size and
	 *   the number of sectors).
	 * @param ID
	 *   Contains manufacturer and device ID for this flash.
	 * @param use12bitAddressing set to true for 12-bit command addresses, false for 11-bit command addresses
	 * @param config The motherboard this flash belongs to
	 * @param load Load initial content (hack for 'Matra INK')
	 */
	AmdFlash(const Rom& rom, const std::vector<SectorInfo>& sectorInfo,
	         word ID, bool use12bitAddressing,
	         const DeviceConfig& config, bool load = true);
	~AmdFlash();

	void reset();

	unsigned getSize() const;
	byte read(unsigned address);
	byte peek(unsigned address) const;
	void write(unsigned address, byte value);
	const byte* getReadCacheLine(unsigned address) const;

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

//private:
	struct AmdCmd {
		unsigned addr;
		byte value;

		template<typename Archive>
		void serialize(Archive& ar, unsigned version);
	};

	enum State { ST_IDLE, ST_IDENT };

private:
	void getSectorInfo(unsigned address, unsigned& sector,
                           unsigned& sectorSize, unsigned& offset) const;

	void setState(State newState);
	bool checkCommandReset();
	bool checkCommandEraseSector();
	bool checkCommandEraseChip();
	bool checkCommandProgramHelper(unsigned, const byte*, size_t cmdLen);
	bool checkCommandProgram();
	bool checkCommandQuadrupleByteProgram();
	bool checkCommandManifacturer();
	bool partialMatch(unsigned len, const byte* dataSeq) const;

	MSXMotherBoard& motherBoard;
	const Rom& rom;
	std::unique_ptr<SRAM> ram;
	MemBuffer<int> writeAddress;
	MemBuffer<const byte*> readAddress;
	const std::vector<SectorInfo> sectorInfo;
	const unsigned size;
	const word ID;
	const bool use12bitAddressing;

	static const unsigned MAX_CMD_SIZE = 8;
	AmdCmd cmd[MAX_CMD_SIZE];
	unsigned cmdIdx;
	State state;
};

} // namespace openmsx

#endif
