#include "i2c_p.hpp"

#ifdef KOVAN
#include <i2c_wrapper.h>
#endif

#define DEVICE_NAME "1"

bool Private::I2C::pickSlave(const char *slave)
{
	if(m_fd < 0) return false;
#ifdef KOVAN
	return i2c_pick_slave(m_fd, slave) >= 0;
#else
	return false;
#endif
}

bool Private::I2C::write(const unsigned char &addr, const unsigned char &val, const bool &readback)
{
	if(m_fd < 0) return false;
#ifdef KOVAN
	return i2c_write_byte(m_fd, addr, val, readback ? 1 : 0) >= 0;
#else
	return false;
#endif
}

unsigned char Private::I2C::read(const unsigned char &addr)
{
	if(m_fd < 0) return 0;
#ifdef KOVAN
	return i2c_read_byte(m_fd, addr);
#else
	return 0;
#endif
}

Private::I2C *Private::I2C::instance()
{
	static I2C s_instance;
	return &s_instance;
}

Private::I2C::I2C()
	: m_fd(-1)
{
	char dummy[20];
#ifdef KOVAN
	m_fd = i2c_open_device(DEVICE_NAME, dummy, sizeof(dummy), 0);
#endif
}

Private::I2C::~I2C()
{
	if(m_fd < 0) return;
#ifdef KOVAN
	i2c_close_device(m_fd);
#endif
}