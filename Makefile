include $(TOPDIR)/rules.mk

PKG_NAME:=vibration-monitor
PKG_VERSION:=1.11.2
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/vibration-monitor
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Vibration Monitor
  DEPENDS:=+libstdcpp +libcurl
endef

define Package/vibration-monitor/description
  Vibration monitoring utility
endef

TARGET_CFLAGS += -I$(PKG_BUILD_DIR)/libs
TARGET_LDFLAGS += -L$(STAGING_DIR)/usr/lib -lcurl

define Build/Prepare
		mkdir -p $(PKG_BUILD_DIR)
		$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
		$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CFLAGS) \
				$(TARGET_LDFLAGS) \
				-o $(PKG_BUILD_DIR)/vibration-monitor $(PKG_BUILD_DIR)/main.cpp \
				-lcurl
endef

define Package/vibration-monitor/install
		$(INSTALL_DIR) $(1)/usr/bin
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/vibration-monitor $(1)/usr/bin/
endef

$(eval $(call BuildPackage,vibration-monitor))