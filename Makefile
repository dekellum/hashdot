
# JDK used for compiling, a different one can be set at runtime,
# see profiles/default.hdp
JAVA_HOME?=$(realpath $(dir $(shell which java))/..)

# Dependent on apache portable runtime (apr-devel-1.x apr-1.x)
APR_CONFIG=$(shell which apr-1-config)

# Install hashdot binaries to specified directory
INSTALL_BIN=/opt/bin

# Where to install and find profiles (*.hdp) 
# export PROFILE_DIR=./profiles to work in source directory
PROFILE_DIR?=/opt/hashdot/profiles

VERSION=1.2

CC=gcc
CFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -O2 -Wall -fno-strict-aliasing -g \
-I$(JAVA_HOME)/include \
-I$(JAVA_HOME)/include/linux \
-DHASHDOT_PROFILE_DIR=\"${PROFILE_DIR}\" \
-DHASHDOT_VERSION=\"${VERSION}\"

LDFLAGS=$(shell ${APR_CONFIG} --ldflags)
LDLIBS=$(shell ${APR_CONFIG} --libs --link-ld)

all: hashdot

# Note: You might want to install different symlinks below. These offer
# convenient shortcuts to same-named profiles.

install: hashdot
	install -d $(PROFILE_DIR)
	install -m 644 profiles/*.hdp $(PROFILE_DIR)
	install -m 755 hashdot $(INSTALL_BIN)
	cd $(INSTALL_BIN) && test -e jruby || ln -s hashdot jruby
	cd $(INSTALL_BIN) && test -e jruby-shortlived || ln -s hashdot jruby-shortlived

dist: hashdot
	mkdir hashdot-$(VERSION)
	cp -a INSTALL Makefile hashdot.c profiles test hashdot-$(VERSION)
	mkdir hashdot-$(VERSION)/doc
	cp -a doc/COPYING doc/index.html doc/NOTICE doc/reference.html doc/style.css hashdot-$(VERSION)/doc
	tar --exclude '.svn' --exclude '*~' -zcvf hashdot-$(VERSION)-src.tar.gz hashdot-$(VERSION)
	rm -rf hashdot-$(VERSION)

jruby: hashdot
	ln -s hashdot jruby

test: hashdot jruby
	test/error/error_tests.sh
	test/list_libs 
	test/cmdline param1 param2
	test/test_props.rb 
	test/test_env.rb
	test/test_chdir.rb
	examples/hello.rb

clean: 
	rm -rf hashdot-$(VERSION)-src.tar.gz hashdot hashdot.dSYM

.PHONY : test all install dist
