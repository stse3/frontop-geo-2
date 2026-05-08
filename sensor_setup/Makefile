include $(TOPDIR)/rules.mk

PKG_NAME:=sensor-setup
PKG_VERSION:=1.0.1
PKG_RELEASE:=2

include $(INCLUDE_DIR)/package.mk

define Package/sensor-setup
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Sensor Setup Utility
  DEPENDS:=+libstdcpp +libcurl
endef

define Package/sensor-setup/description
  Utility for setting up sensor devices via serial port
endef

# Set C++ standard and include paths
TARGET_CXXFLAGS += -std=c++17
TARGET_CFLAGS += -I$(PKG_BUILD_DIR) -I$(PKG_BUILD_DIR)/include
TARGET_LDFLAGS += -L$(STAGING_DIR)/usr/lib -lcurl

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)/include/nlohmann
	$(CP) ./src/* $(PKG_BUILD_DIR)/
	$(CP) ./src/json.hpp $(PKG_BUILD_DIR)/include/nlohmann/
endef

define Build/Configure
	# No configuration needed
endef

define Build/Compile
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CFLAGS) \
		-Wall -Wextra -pedantic -pthread \
		-o $(PKG_BUILD_DIR)/sensor-setup $(PKG_BUILD_DIR)/sensor-setup.cpp \
		$(TARGET_LDFLAGS)
endef

define Package/sensor-setup/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sensor-setup $(1)/usr/bin/
endef

$(eval $(call BuildPackage,sensor-setup))