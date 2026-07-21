include $(TOPDIR)/rules.mk

PKG_NAME := vibration-monitor
PKG_VERSION := 2.0
PKG_RELEASE := 3

include $(INCLUDE_DIR)/package.mk

define Package/vibration-monitor
	SECTION := utils
	CATEGORY := Utilities
	TITLE := Vibration Monitor
	DEPENDS := +libstdcpp +libcurl +libuci +libubox
endef

define Package/vibration-monitor/description
	Vibration monitoring utility for sensor data collection and analysis
endef

define Package/vibration-monitor/conffiles
	/etc/config/vibration
endef

MAKE_FLAGS += \
	CXXFLAGS="$(TARGET_CXXFLAGS) -std=c++17" \
    LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib"

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/  # Ensure 'src' exists and contains needed files
endef

define Build/Compile
	$(TARGET_CXX) \
		$(CXXFLAGS) \
		$(TARGET_CPPFLAGS) \
		$(PKG_BUILD_DIR)/main.cpp \
		$(LDFLAGS) \
		-lcurl \
		-luci \
		-lubox \
		-o $(PKG_BUILD_DIR)/vibration-monitor
endef

define Package/vibration-monitor/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/vibration-monitor $(1)/usr/bin/
endef

$(eval $(call BuildPackage,vibration-monitor))