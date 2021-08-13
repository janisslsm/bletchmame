/***************************************************************************

	info_builder.cpp

	Code to build MAME info DB

***************************************************************************/

#include <cmath>

#include "info_builder.h"


//**************************************************************************
//  LOCALS
//**************************************************************************

static const util::enum_parser<info::rom::dump_status_t> s_dump_status_parser =
{
	{ "baddump", info::rom::dump_status_t::BADDUMP },
	{ "nodump", info::rom::dump_status_t::NODUMP },
	{ "good", info::rom::dump_status_t::GOOD }
};


static const util::enum_parser<info::software_list::status_type> s_status_parser =
{
	{ "original", info::software_list::status_type::ORIGINAL, },
	{ "compatible", info::software_list::status_type::COMPATIBLE }
};


static const util::enum_parser<info::configuration_condition::relation_t> s_relation_parser =
{
	{ "eq", info::configuration_condition::relation_t::EQ },
	{ "ne", info::configuration_condition::relation_t::NE },
	{ "gt", info::configuration_condition::relation_t::GT },
	{ "le", info::configuration_condition::relation_t::LE },
	{ "lt", info::configuration_condition::relation_t::LT },
	{ "ge", info::configuration_condition::relation_t::GE }
};


static const util::enum_parser<info::feature::type_t> s_feature_type_parser =
{
	{ "protection",	info::feature::type_t::PROTECTION },
	{ "timing",		info::feature::type_t::TIMING },
	{ "graphics",	info::feature::type_t::GRAPHICS },
	{ "palette",	info::feature::type_t::PALETTE },
	{ "sound",		info::feature::type_t::SOUND },
	{ "capture",	info::feature::type_t::CAPTURE },
	{ "camera",		info::feature::type_t::CAMERA },
	{ "microphone",	info::feature::type_t::MICROPHONE },
	{ "controls",	info::feature::type_t::CONTROLS },
	{ "keyboard",	info::feature::type_t::KEYBOARD },
	{ "mouse",		info::feature::type_t::MOUSE },
	{ "media",		info::feature::type_t::MEDIA },
	{ "disk",		info::feature::type_t::DISK },
	{ "printer",	info::feature::type_t::PRINTER },
	{ "tape",		info::feature::type_t::TAPE },
	{ "punch",		info::feature::type_t::PUNCH },
	{ "drum",		info::feature::type_t::DRUM },
	{ "rom",		info::feature::type_t::ROM },
	{ "comms",		info::feature::type_t::COMMS },
	{ "lan",		info::feature::type_t::LAN },
	{ "wan",		info::feature::type_t::WAN },
};


static const util::enum_parser<info::feature::quality_t> s_feature_quality_parser =
{
	{ "unemulated",	info::feature::quality_t::UNEMULATED },
	{ "imperfect",	info::feature::quality_t::IMPERFECT }
};


static const util::enum_parser<info::chip::type_t> s_chip_type_parser =
{
	{ "cpu", info::chip::type_t::CPU },
	{ "audio", info::chip::type_t::AUDIO }
};


static const util::enum_parser<info::display::type_t> s_display_type_parser =
{
	{ "unknown", info::display::type_t::UNKNOWN },
	{ "raster", info::display::type_t::RASTER },
	{ "vector", info::display::type_t::VECTOR },
	{ "lcd", info::display::type_t::LCD },
	{ "svg", info::display::type_t::SVG }
};


static const util::enum_parser<info::display::rotation_t> s_display_rotation_parser =
{
	{ "0", info::display::rotation_t::ROT0 },
	{ "90", info::display::rotation_t::ROT90 },
	{ "180", info::display::rotation_t::ROT180 },
	{ "270", info::display::rotation_t::ROT270 }
};


static const util::enum_parser<info::machine::driver_quality_t> s_driver_quality_parser =
{
	{ "good", info::machine::driver_quality_t::GOOD },
	{ "imperfect", info::machine::driver_quality_t::IMPERFECT },
	{ "preliminary", info::machine::driver_quality_t::PRELIMINARY }
};


static const util::enum_parser<bool> s_supported_parser =
{
	{ "supported", true },
	{ "unsupported", false }
};


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

//-------------------------------------------------
//  to_uint32
//-------------------------------------------------

template<typename T>
static std::uint32_t to_uint32(T &&value)
{
	std::uint32_t new_value = static_cast<std::uint32_t>(value);
	if (new_value != value)
		throw std::logic_error("Array size cannot fit in 32 bits");
	return new_value;
};


//-------------------------------------------------
//  writeVectorData
//-------------------------------------------------

template<typename T>
static void writeVectorData(QIODevice &stream, const std::vector<T> &vector)
{
	stream.write((const char *)vector.data(), vector.size() * sizeof(T));
}


//-------------------------------------------------
//  encodeBool
//-------------------------------------------------

static constexpr std::uint8_t encodeBool(std::optional<bool> b, std::uint8_t defaultValue = 0xFF)
{
	return b.has_value()
		? (*b ? 0x01 : 0x00)
		: defaultValue;
}


//-------------------------------------------------
//  encodeEnum
//-------------------------------------------------

template<typename T>
static constexpr std::uint8_t encodeEnum(std::optional<T> &&value, std::uint8_t defaultValue = 0)
{
	return value.has_value()
		? (std::uint8_t) value.value()
		: defaultValue;
}


//-------------------------------------------------
//  binaryFromHex
//-------------------------------------------------

template<int N>
static bool binaryFromHex(std::uint8_t (&dest)[N], const std::optional<std::string> &hex)
{
	std::size_t pos = hex.has_value() ? util::binaryFromHex(dest, *hex) : 0;
	std::fill(dest + pos, dest + N, 0);
	return pos == N;
};


//-------------------------------------------------
//  process_xml()
//-------------------------------------------------

bool info::database_builder::process_xml(QIODevice &input, QString &error_message)
{
	// sanity check; ensure we're fresh
	assert(m_machines.empty());
	assert(m_devices.empty());

	// prepare header and magic variables
	info::binaries::header header = { 0, };
	header.m_magic = info::binaries::MAGIC_HDR;
	header.m_sizes_hash = info::database::calculate_sizes_hash();

	// reserve space based on what we know about MAME 0.229
	m_biossets.reserve(36000);					// 33456 bios sets
	m_roms.reserve(350000);						// 324459 roms
	m_disks.reserve(1400);						// 1132 disks
	m_machines.reserve(48000);					// 44609 machines
	m_devices.reserve(11000);					// 10272 devices
	m_features.reserve(22000);					// 20022 features
	m_chips.reserve(180000);					// 170244 chips
	m_displays.reserve(0);						// TODO displays
	m_samples.reserve(20000);					// 18538 samples
	m_configurations.reserve(600000);			// 548738 configurations
	m_configuration_conditions.reserve(7500);	// 6769 conditions
	m_configuration_settings.reserve(1700000);	// 1623524 settings
	m_software_lists.reserve(6200);				// 5684 software lists
	m_ram_options.reserve(6500);				// 5718 ram options

	// parse the -listxml output
	XmlParser xml;
	std::string current_device_extensions;
	xml.onElementBegin({ "mame" }, [this, &header](const XmlParser::Attributes &attributes)
	{
		header.m_build_strindex = m_strings.get(attributes, "build");
	});
	xml.onElementBegin({ "mame", "machine" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::machine &machine = m_machines.emplace_back();
		machine.m_runnable				= encodeBool(attributes.get<bool>("runnable").value_or(true));
		machine.m_name_strindex			= m_strings.get(attributes, "name");
		machine.m_sourcefile_strindex	= m_strings.get(attributes, "sourcefile");
		machine.m_clone_of_machindex	= m_strings.get(attributes, "cloneof");		// string index for now; changes to machine index later
		machine.m_rom_of_machindex		= m_strings.get(attributes, "romof");		// string index for now; changes to machine index later
		machine.m_is_bios				= encodeBool(attributes.get<bool>("isbios"));
		machine.m_is_device				= encodeBool(attributes.get<bool>("isdevice"));
		machine.m_is_mechanical			= encodeBool(attributes.get<bool>("ismechanical"));
		machine.m_biossets_index		= to_uint32(m_biossets.size());
		machine.m_biossets_count		= 0;
		machine.m_roms_index			= to_uint32(m_roms.size());
		machine.m_roms_count			= 0;
		machine.m_disks_index			= to_uint32(m_disks.size());
		machine.m_disks_count			= 0;
		machine.m_features_index		= to_uint32(m_features.size());
		machine.m_features_count		= 0;
		machine.m_chips_index			= to_uint32(m_chips.size());
		machine.m_chips_count			= 0;
		machine.m_displays_index		= to_uint32(m_displays.size());
		machine.m_displays_count		= 0;
		machine.m_samples_index			= to_uint32(m_samples.size());
		machine.m_samples_count			= 0;
		machine.m_configurations_index	= to_uint32(m_configurations.size());
		machine.m_configurations_count	= 0;
		machine.m_software_lists_index	= to_uint32(m_software_lists.size());
		machine.m_software_lists_count	= 0;
		machine.m_ram_options_index		= to_uint32(m_ram_options.size());
		machine.m_ram_options_count		= 0;
		machine.m_devices_index			= to_uint32(m_devices.size());
		machine.m_devices_count			= 0;
		machine.m_slots_index			= to_uint32(m_slots.size());
		machine.m_slots_count			= 0;
		machine.m_description_strindex	= 0;
		machine.m_year_strindex			= 0;
		machine.m_manufacturer_strindex = 0;
		machine.m_quality_status		= 0;
		machine.m_quality_emulation		= 0;
		machine.m_quality_cocktail		= 0;
		machine.m_save_state_supported	= encodeBool(std::nullopt);
		machine.m_unofficial			= encodeBool(std::nullopt);
		machine.m_incomplete			= encodeBool(std::nullopt);
		machine.m_sound_channels		= ~0;
	});
	xml.onElementEnd({ "mame", "machine", "description" }, [this](QString &&content)
	{
		util::last(m_machines).m_description_strindex = m_strings.get(content);
	});
	xml.onElementEnd({ "mame", "machine", "year" }, [this](QString &&content)
	{
		util::last(m_machines).m_year_strindex = m_strings.get(content);
	});
	xml.onElementEnd({ "mame", "machine", "manufacturer" }, [this](QString &&content)
	{
		util::last(m_machines).m_manufacturer_strindex = m_strings.get(content);
	});
	xml.onElementBegin({ "mame", "machine", "biosset" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::biosset &biosset = m_biossets.emplace_back();
		biosset.m_name_strindex				= m_strings.get(attributes, "name");
		biosset.m_description_strindex		= m_strings.get(attributes, "description");
		biosset.m_default					= encodeBool(attributes.get<bool>("default").value_or(false));
		util::last(m_machines).m_biossets_count++;
	});
	xml.onElementBegin({ "mame", "machine", "rom" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::rom &rom = m_roms.emplace_back();
		rom.m_name_strindex					= m_strings.get(attributes, "name");
		rom.m_bios_strindex					= m_strings.get(attributes, "bios");
		rom.m_size							= attributes.get<std::uint32_t>("size").value_or(0);
		binaryFromHex(rom.m_crc,			  attributes.get<std::string>("crc"));
		binaryFromHex(rom.m_sha1,			  attributes.get<std::string>("sha1"));
		rom.m_size							= attributes.get<std::uint32_t>("size").value_or(0);
		rom.m_merge_strindex				= m_strings.get(attributes, "merge");
		rom.m_region_strindex				= m_strings.get(attributes, "region");
		rom.m_offset						= attributes.get<std::uint64_t>("offset", 16).value_or(0);
		rom.m_status						= encodeEnum(attributes.get<info::rom::dump_status_t>("status", s_dump_status_parser));
		rom.m_optional						= encodeBool(attributes.get<bool>("optional").value_or(false));
		util::last(m_machines).m_roms_count++;
	});
	xml.onElementBegin({ "mame", "machine", "disk" }, [this](const XmlParser::Attributes &attributes)
	{
		std::string data;
		bool b;
		info::disk::dump_status_t dump_status;

		info::binaries::disk &disk = m_disks.emplace_back();
		disk.m_name_strindex				= m_strings.get(attributes, "name");
		binaryFromHex(disk.m_sha1,			  attributes.get<std::string>("sha1"));
		disk.m_merge_strindex				= m_strings.get(attributes, "merge");
		disk.m_region_strindex				= m_strings.get(attributes, "region");
		disk.m_index						= attributes.get<std::uint32_t>("index").value_or(0);
		disk.m_writable						= encodeBool(attributes.get<bool>("writable").value_or(false));
		disk.m_status						= encodeEnum(attributes.get<info::rom::dump_status_t>("status", s_dump_status_parser));
		disk.m_optional						= encodeBool(attributes.get<bool>("optional").value_or(false));
		util::last(m_machines).m_disks_count++;
	});
	xml.onElementBegin({ "mame", "machine", "feature" }, [this, &current_device_extensions](const XmlParser::Attributes &attributes)
	{
		info::binaries::feature &feature = m_features.emplace_back();
		feature.m_type		= encodeEnum(attributes.get<info::feature::type_t>		("type", s_feature_type_parser));
		feature.m_status	= encodeEnum(attributes.get<info::feature::quality_t>	("status", s_feature_quality_parser));
		feature.m_overall	= encodeEnum(attributes.get<info::feature::quality_t>	("overall", s_feature_quality_parser));
		util::last(m_machines).m_features_count++;
	});
	xml.onElementBegin({ "mame", "machine", "chip" }, [this, &current_device_extensions](const XmlParser::Attributes &attributes)
	{
		info::binaries::chip &chip = m_chips.emplace_back();
		chip.m_type				= encodeEnum(attributes.get<info::chip::type_t>("type", s_chip_type_parser));
		chip.m_name_strindex	= m_strings.get(attributes, "name");
		chip.m_tag_strindex		= m_strings.get(attributes, "tag");
		chip.m_clock			= attributes.get<std::uint64_t>("clock").value_or(0);

		util::last(m_machines).m_chips_count++;
	});
	xml.onElementBegin({ "mame", "machine", "display" }, [this, &current_device_extensions](const XmlParser::Attributes &attributes)
	{
		info::binaries::display &display = m_displays.emplace_back();
		display.m_tag_strindex	= m_strings.get(attributes, "tag");
		display.m_width			= attributes.get<std::uint32_t>("width").value_or(~0);
		display.m_height		= attributes.get<std::uint32_t>("height").value_or(~0);
		display.m_refresh		= attributes.get<float>("refresh").value_or(NAN);
		display.m_pixclock		= attributes.get<std::uint64_t>("pixclock").value_or(~0);
		display.m_htotal		= attributes.get<std::uint32_t>("htotal").value_or(~0);
		display.m_hbend			= attributes.get<std::uint32_t>("hbend").value_or(~0);
		display.m_hbstart		= attributes.get<std::uint32_t>("hbstart").value_or(~0);
		display.m_vtotal		= attributes.get<std::uint32_t>("vtotal").value_or(~0);
		display.m_vbend			= attributes.get<std::uint32_t>("vbend").value_or(~0);
		display.m_vbstart		= attributes.get<std::uint32_t>("vbstart").value_or(~0);
		display.m_type			= encodeEnum(attributes.get<info::display::type_t>("type", s_display_type_parser));
		display.m_rotate		= encodeEnum(attributes.get<info::display::rotation_t>("rotate", s_display_rotation_parser));
		display.m_flipx			= encodeBool(attributes.get<bool>("flipx"));
		util::last(m_machines).m_displays_count++;
	});
	xml.onElementBegin({ "mame", "machine", "sample" }, [this, &current_device_extensions](const XmlParser::Attributes &attributes)
	{
		info::binaries::sample &sample = m_samples.emplace_back();
		sample.m_name_strindex	= m_strings.get(attributes, "name");
		util::last(m_machines).m_samples_count++;
	});
	xml.onElementBegin({ { "mame", "machine", "configuration" },
						 { "mame", "machine", "dipswitch" } }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::configuration &configuration = m_configurations.emplace_back();
		configuration.m_name_strindex					= m_strings.get(attributes, "name");
		configuration.m_tag_strindex					= m_strings.get(attributes, "tag");
		configuration.m_mask							= attributes.get<std::uint32_t>("mask").value_or(0);
		configuration.m_configuration_settings_index	= to_uint32(m_configuration_settings.size());
		configuration.m_configuration_settings_count	= 0;
	
		util::last(m_machines).m_configurations_count++;
	});
	xml.onElementBegin({ { "mame", "machine", "configuration", "confsetting" },
						 { "mame", "machine", "dipswitch", "dipvalue" } }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::configuration_setting &configuration_setting = m_configuration_settings.emplace_back();
		configuration_setting.m_name_strindex		= m_strings.get(attributes, "name");
		configuration_setting.m_conditions_index	= to_uint32(m_configuration_conditions.size());
		configuration_setting.m_value				= attributes.get<std::uint32_t>("value").value_or(0);

		util::last(m_configurations).m_configuration_settings_count++;
	});
	xml.onElementBegin({ { "mame", "machine", "configuration", "confsetting", "condition" },
						 { "mame", "machine", "dipswitch", "dipvalue", "condition" } }, [this](const XmlParser::Attributes &attributes)
	{
		std::string data;
		info::binaries::configuration_condition &configuration_condition = m_configuration_conditions.emplace_back();
		configuration_condition.m_tag_strindex			= m_strings.get(attributes, "tag");
		configuration_condition.m_relation				= encodeEnum(attributes.get<info::configuration_condition::relation_t>("relation", s_relation_parser));
		configuration_condition.m_mask					= attributes.get<std::uint32_t>("mask").value_or(0);
		configuration_condition.m_value					= attributes.get<std::uint32_t>("value").value_or(0);
	});
	xml.onElementBegin({ "mame", "machine", "device" }, [this, &current_device_extensions](const XmlParser::Attributes &attributes)
	{
		info::binaries::device &device = m_devices.emplace_back();
		device.m_type_strindex			= m_strings.get(attributes, "type");
		device.m_tag_strindex			= m_strings.get(attributes, "tag");
		device.m_interface_strindex		= m_strings.get(attributes, "interface");
		device.m_mandatory				= encodeBool(attributes.get<bool>("mandatory").value_or(false));
		device.m_instance_name_strindex	= 0;
		device.m_extensions_strindex	= 0;

		current_device_extensions.clear();

		util::last(m_machines).m_devices_count++;
	});
	xml.onElementBegin({ "mame", "machine", "device", "instance" }, [this](const XmlParser::Attributes &attributes)
	{
		util::last(m_devices).m_instance_name_strindex = m_strings.get(attributes, "name");
	});
	xml.onElementBegin({ "mame", "machine", "device", "extension" }, [this, &current_device_extensions](const XmlParser::Attributes &attributes)
	{
		std::optional<std::string> name = attributes.get<std::string>("name");
		if (name)
		{
			current_device_extensions.append(*name);
			current_device_extensions.append(",");
		}
	});
	xml.onElementEnd({ "mame", "machine", "device" }, [this, &current_device_extensions](QString &&)
	{
		if (!current_device_extensions.empty())
			util::last(m_devices).m_extensions_strindex = m_strings.get(current_device_extensions);
	});
	xml.onElementBegin({ "mame", "machine", "driver" }, [this, &current_device_extensions](const XmlParser::Attributes &attributes)
	{
		info::binaries::machine &machine = util::last(m_machines);
		machine.m_quality_status		= encodeEnum(attributes.get<info::machine::driver_quality_t>("status",		s_driver_quality_parser),	machine.m_quality_status);
		machine.m_quality_emulation		= encodeEnum(attributes.get<info::machine::driver_quality_t>("emulation",	s_driver_quality_parser),	machine.m_quality_emulation);
		machine.m_quality_cocktail		= encodeEnum(attributes.get<info::machine::driver_quality_t>("cocktail",	s_driver_quality_parser),	machine.m_quality_cocktail);
		machine.m_save_state_supported	= encodeBool(attributes.get<bool>							("savestate",	s_supported_parser),		machine.m_save_state_supported);
		machine.m_unofficial			= encodeBool(attributes.get<bool>							("unofficial"),								machine.m_unofficial);
		machine.m_incomplete			= encodeBool(attributes.get<bool>							("incomplete"),								machine.m_incomplete);
	});
	xml.onElementBegin({ "mame", "machine", "slot" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::slot &slot = m_slots.emplace_back();
		slot.m_name_strindex					= m_strings.get(attributes, "name");
		slot.m_slot_options_index				= to_uint32(m_slot_options.size());
		slot.m_slot_options_count				= 0;
		util::last(m_machines).m_slots_count++;
	});
	xml.onElementBegin({ "mame", "machine", "slot", "slotoption" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::slot_option &slot_option = m_slot_options.emplace_back();
		slot_option.m_name_strindex				= m_strings.get(attributes, "name");
		slot_option.m_devname_strindex			= m_strings.get(attributes, "devname");
		slot_option.m_is_default				= encodeBool(attributes.get<bool>("default").value_or(false));
		util::last(m_slots).m_slot_options_count++;
	});
	xml.onElementBegin({ "mame", "machine", "softwarelist" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::software_list &software_list = m_software_lists.emplace_back();
		software_list.m_name_strindex			= m_strings.get(attributes, "name");
		software_list.m_filter_strindex			= m_strings.get(attributes, "filter");
		software_list.m_status					= encodeEnum(attributes.get<info::software_list::status_type>("status", s_status_parser));
		util::last(m_machines).m_software_lists_count++;
	});
	xml.onElementBegin({ "mame", "machine", "ramoption" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::ram_option &ram_option = m_ram_options.emplace_back();
		ram_option.m_name_strindex				= m_strings.get(attributes, "name");
		ram_option.m_is_default					= encodeBool(attributes.get<bool>("default").value_or(false));
		ram_option.m_value						= 0;
		util::last(m_machines).m_ram_options_count++;
	});
	xml.onElementEnd({ "mame", "machine", "ramoption" }, [this](QString &&content)
	{
		bool ok;
		unsigned long val = content.toULong(&ok);
		util::last(m_ram_options).m_value = ok ? val : 0;
	});
	xml.onElementBegin({ "mame", "machine", "sound" }, [this](const XmlParser::Attributes &attributes)
	{
		info::binaries::machine &machine = util::last(m_machines);
		machine.m_sound_channels		= attributes.get<std::uint8_t>("channels").value_or(~0);
	});

	// parse!
	bool success;
	try
	{
		success = xml.parse(input);
	}
	catch (std::exception &ex)
	{
		// did an exception (probably thrown by to_uint32) get thrown?
		error_message = ex.what();
		return false;
	}
	if (!success)
	{
		// now check for XML parsing errors; this is likely the result of somebody aborting the DB rebuild, but
		// it is the caller's responsibility to handle that situation
		error_message = xml.errorMessagesSingleString();
		return false;
	}

	// final magic bytes on string table
	m_strings.embed_value(info::binaries::MAGIC_STRINGTABLE_END);

	// finalize the header
	header.m_machines_count					= to_uint32(m_machines.size());
	header.m_biossets_count					= to_uint32(m_biossets.size());
	header.m_roms_count						= to_uint32(m_roms.size());
	header.m_disks_count					= to_uint32(m_disks.size());
	header.m_devices_count					= to_uint32(m_devices.size());
	header.m_slots_count					= to_uint32(m_slots.size());
	header.m_slot_options_count				= to_uint32(m_slot_options.size());
	header.m_features_count					= to_uint32(m_features.size());
	header.m_chips_count					= to_uint32(m_chips.size());
	header.m_displays_count					= to_uint32(m_displays.size());
	header.m_samples_count					= to_uint32(m_samples.size());
	header.m_configurations_count			= to_uint32(m_configurations.size());
	header.m_configuration_settings_count	= to_uint32(m_configuration_settings.size());
	header.m_configuration_conditions_count	= to_uint32(m_configuration_conditions.size());
	header.m_software_lists_count			= to_uint32(m_software_lists.size());
	header.m_ram_options_count				= to_uint32(m_ram_options.size());

	// and salt it
	m_salted_header = util::salt(header, info::binaries::salt());

	// sort machines by name to facilitate lookups
	std::sort(
		m_machines.begin(),
		m_machines.end(),
		[this](const binaries::machine &a, const binaries::machine &b)
		{
			string_table::SsoBuffer ssoBufferA, ssoBufferB;
			const char *aText = m_strings.lookup(a.m_name_strindex, ssoBufferA);
			const char *bText = m_strings.lookup(b.m_name_strindex, ssoBufferB);
			return strcmp(aText, bText) < 0;
		});

	// build a machine index map
	std::unordered_map<std::uint32_t, std::uint32_t> machineIndexMap;
	machineIndexMap.reserve(m_machines.size() + 1);
	machineIndexMap.emplace(m_strings.get(std::string()), ~0);
	for (auto iter = m_machines.begin(); iter != m_machines.end(); iter++)
	{
		machineIndexMap.emplace(iter->m_name_strindex, iter - m_machines.begin());
	}

	// helper to perform machine index lookups
	auto machineIndexFromStringIndex = [&machineIndexMap](std::uint32_t stringIndex)
	{
		auto iter = machineIndexMap.find(stringIndex);
		return iter != machineIndexMap.end()
			? iter->second
			: ~0;	// should never happen unless -listxml is returning bad results
	};

	// and change clone_of and rom_of to be machine indexes, using the map we have above
	for (info::binaries::machine &machine : m_machines)
	{
		machine.m_clone_of_machindex = machineIndexFromStringIndex(machine.m_clone_of_machindex);
		machine.m_rom_of_machindex = machineIndexFromStringIndex(machine.m_rom_of_machindex);
	}

	// success!
	error_message.clear();
	return true;
}


//-------------------------------------------------
//  emit_info
//-------------------------------------------------

void info::database_builder::emit_info(QIODevice &output) const
{
	output.write((const char *) &m_salted_header, sizeof(m_salted_header));
	writeVectorData(output, m_machines);
	writeVectorData(output, m_biossets);
	writeVectorData(output, m_roms);
	writeVectorData(output, m_disks);
	writeVectorData(output, m_devices);
	writeVectorData(output, m_slots);
	writeVectorData(output, m_slot_options);
	writeVectorData(output, m_features);
	writeVectorData(output, m_chips);
	writeVectorData(output, m_displays);
	writeVectorData(output, m_samples);
	writeVectorData(output, m_configurations);
	writeVectorData(output, m_configuration_settings);
	writeVectorData(output, m_configuration_conditions);
	writeVectorData(output, m_software_lists);
	writeVectorData(output, m_ram_options);
	writeVectorData(output, m_strings.data());
}


//-------------------------------------------------
//  string_table ctor
//-------------------------------------------------

info::database_builder::string_table::string_table()
{
	// reserve space based on expected size (see comments above)
	m_data.reserve(4500000);		// 4326752 bytes
	m_map.reserve(300000);			// 264907 entries

	// embed the initial magic bytes
	embed_value(info::binaries::MAGIC_STRINGTABLE_BEGIN);
}


//-------------------------------------------------
//  string_table::get(const std::string &s)
//-------------------------------------------------

std::uint32_t info::database_builder::string_table::get(const std::string &s)
{
	// try encoding as a small string
	std::optional<std::uint32_t> ssoResult = info::database::tryEncodeAsSmallString(s);
	if (ssoResult)
		return *ssoResult;

	// if we've already cached this value, look it up
	auto iter = m_map.find(s);
	if (iter != m_map.end())
		return iter->second;

	// we're going to append the string; the current size becomes the position of the new string
	std::uint32_t result = to_uint32(m_data.size());

	// append the string (including trailing NUL) to m_data
	m_data.insert(m_data.end(), s.c_str(), s.c_str() + s.size() + 1);

	// and to m_map
	m_map[s] = result;

	// and return
	return result;
}


//-------------------------------------------------
//  string_table::get(const QString &s)
//-------------------------------------------------

std::uint32_t info::database_builder::string_table::get(const QString &s)
{
	// this is safe because QString::toStdString() specified UTF-8
	return get(s.toStdString());
}


//-------------------------------------------------
//  string_table::get(const XmlParser::Attributes &attributes, const char *attribute)
//-------------------------------------------------

std::uint32_t info::database_builder::string_table::get(const XmlParser::Attributes &attributes, const char *attribute)
{
	std::optional<QString> attributeValue = attributes.get<QString>(attribute);
	return get(attributeValue ? *attributeValue : QString());
}


//-------------------------------------------------
//  string_table::data
//-------------------------------------------------

const std::vector<char> &info::database_builder::string_table::data() const
{
	return m_data;
}


//-------------------------------------------------
//  string_table::lookup
//-------------------------------------------------

const char *info::database_builder::string_table::lookup(std::uint32_t value, SsoBuffer &ssoBuffer) const
{
	const char *result;

	std::optional<std::array<char, 6>> sso = info::database::tryDecodeAsSmallString(value);
	if (sso)
	{
		ssoBuffer = std::move(*sso);
		result = &ssoBuffer[0];
	}
	else
	{
		assert(value < m_data.size());
		assert(value + strlen(&m_data[value]) < m_data.size());
		result = &m_data[value];
	}
	return result;
}
