include $(TOPDIR)/rules.mk
PKG_NAME:=wzgw
PKG_VERSION:=0.9.2
PKG_RELEASE:=1

SOURCE_DIR:=/home/elux/Downloads/OpenWRT/wzgw
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)


include $(INCLUDE_DIR)/package.mk

define Package/wzgw
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Can MQTT Gateway for IOT
  DEPENDS:=+libuci +libmosquitto +libnl-core +libnl-route +kmod-can +kmod-can-raw +kmod-can-mcp251x
endef

define Package/wzgw/description
MQTT auf CAN Gateway
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(SOURCE_DIR)/src/* $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) \
		$(PKG_BUILD_DIR)/main.c \
		$(PKG_BUILD_DIR)/signals.c \
		$(PKG_BUILD_DIR)/config.c \
		$(PKG_BUILD_DIR)/can.c \
		$(PKG_BUILD_DIR)/mqtt.c \
		$(PKG_BUILD_DIR)/log.c \
		-o $(PKG_BUILD_DIR)/wzgw \
		$(TARGET_LDFLAGS) \
		-lmosquitto \
		-luci \
		-lnl-route-3 \
		-lnl-3
endef

define Package/wzgw/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wzgw $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(SOURCE_DIR)/files/wzgw.init $(1)/etc/init.d/wzgw

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(SOURCE_DIR)/files/wzgw.config $(1)/etc/config/wzgw
endef

$(eval $(call BuildPackage,wzgw))   
