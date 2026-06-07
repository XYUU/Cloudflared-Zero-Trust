# SPDX-License-Identifier: Apache-2.0
#
# OpenWrt package Makefile for cfd. Drop this directory under
# `<openwrt>/package/network/cfd/` and run:
#
#   make package/network/cfd/compile V=s
#
# Produces a .ipk that ships:
#   /usr/sbin/cfd
#   /etc/init.d/cfd          (procd)
#   /etc/config/cfd          (UCI default)
#
# The user is expected to provide /etc/cfd.ini themselves (or scp it after
# first install).

include $(TOPDIR)/rules.mk

PKG_NAME:=cfd
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_LICENSE:=Apache-2.0

PKG_BUILD_DEPENDS:=capnproto

# Point this at your local checkout for now. Replace with PKG_SOURCE_URL
# + PKG_HASH once you cut a tagged release.
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=file://$(TOPDIR)/feeds/cfd
PKG_SOURCE_VERSION:=HEAD

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/cfd
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Cloudflare Zero Trust client (C++)
  URL:=https://github.com/cloudflare/cloudflared
  DEPENDS:=+kmod-tun +libstdcpp +libatomic
endef

define Package/cfd/description
 Lightweight C++ implementation of cloudflared's Tunnel client, suitable for
 embedding on routers and 光猫. Exposes internal CIDRs to Cloudflare WARP
 clients via Zero Trust Tunnel.
endef

CMAKE_OPTIONS += \
    -DCFD_FETCH_MSQUIC=ON \
    -DCFD_FETCH_CAPNP=OFF \
    -DCFD_BUILD_TESTS=OFF \
    -DCFD_STATIC_LINK=ON \
    -DCFD_STRIP_BINARY=ON \
    -DCMAKE_BUILD_TYPE=Release

define Package/cfd/conffiles
/etc/cfd.ini
/etc/config/cfd
endef

define Package/cfd/install
    $(INSTALL_DIR)  $(1)/usr/sbin
    $(INSTALL_BIN)  $(PKG_BUILD_DIR)/src/cfd          $(1)/usr/sbin/cfd
    $(INSTALL_DIR)  $(1)/etc/init.d
    $(INSTALL_BIN)  ./files/etc/init.d/cfd            $(1)/etc/init.d/cfd
    $(INSTALL_DIR)  $(1)/etc/config
    $(INSTALL_CONF) ./files/etc/config/cfd            $(1)/etc/config/cfd
endef

$(eval $(call BuildPackage,cfd))
