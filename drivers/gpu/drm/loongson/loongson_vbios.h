/**
 * struct loongson_vbios - loongson vbios structure
 *
 * @driver_priv: Pointer to driver-private information.
 */
struct loongson_vbios {
	char  title[16];
	uint32_t version_major;
	uint32_t version_minor;
	char  information[20];
	uint32_t crtc_num;
	uint32_t crtc_offset;
	uint32_t connector_num;
	uint32_t connector_offset;
	uint32_t phy_num;
	uint32_t phy_offset;
}__attribute__ ((packed));

enum loongson_crtc_version{
	default_version = 0,
	crtc_version_max = 0xffffffff,
}__attribute__ ((packed));

struct loongson_vbios_crtc{
	uint32_t next_crtc_offset;
	uint32_t crtc_id;
	enum loongson_crtc_version crtc_version;
	uint32_t crtc_max_freq;
	uint32_t crtc_max_weight;
	uint32_t crtc_max_height;
	uint32_t connector_id;
	uint32_t phy_num;
	uint32_t phy_id[2];
}__attribute__ ((packed));

enum loongson_edid_method {
	edid_method_null = 0,
	edid_method_i2c,
	edid_method_vbios,
	edid_method_phy,
	edid_method_max = 0xffffffff,
}__attribute__ ((packed));

enum loongson_vbios_i2c_type{
	i2c_type_null = 0,
	i2c_type_gpio,
	i2c_type_cpu,
	i2c_type_phy,
	i2c_type_max = 0xffffffff,
}__attribute__ ((packed));

struct loongson_vbios_connector{
	uint32_t next_connector_offset;
	uint32_t crtc_id;
	enum loongson_edid_method edid_method;
	enum loongson_vbios_i2c_type i2c_type;
	uint32_t i2c_id;
	uint32_t edid_version;
	uint32_t edid_offset;
}__attribute__ ((packed));

enum loongson_phy_type{
	phy_transparent = 0,
	phy_type_max = 0xffffffff,
}__attribute__ ((packed));

enum hot_swap_method{
	hot_swap_disable = 0,
	hot_swap_polling,
	hot_swap_irq,
	hot_swap_max = 0xffffffff,
}__attribute__ ((packed));

struct loongson_vbios_phy{
	uint32_t next_phy_offset;
	uint32_t crtc_id;
	uint32_t connector_id;
	uint32_t use_internal_edid;
	enum loongson_phy_type phy_type;
	enum loongson_vbios_i2c_type i2c_type;
	uint32_t i2c_id;
	enum hot_swap_method hot_swap_method;
	uint32_t hot_swap_irq;
}__attribute__ ((packed));


