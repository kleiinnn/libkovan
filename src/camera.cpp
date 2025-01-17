#include "kovan/camera.hpp"
#include "kovan/ardrone.hpp"
#include "channel_p.hpp"
#include "camera_c_p.hpp"
#include "warn.hpp"

#include <iostream>
#include <fstream>
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
using namespace Camera;
using namespace std;
// Object //

Camera::Object::Object(const Point2<unsigned> &centroid,
	const Rect<unsigned> &boundingBox,
	const double &confidence, const char *data,
	const size_t &dataLength)
	: m_centroid(centroid),
	m_boundingBox(boundingBox),
	m_confidence(confidence),
	m_data(0),
	m_dataLength(dataLength)
{
	if(!data) return;
	
	m_data = new char[m_dataLength];
	memcpy(m_data, data, m_dataLength);
}

Camera::Object::Object(const Object &rhs)
	: m_centroid(rhs.m_centroid),
	m_boundingBox(rhs.m_boundingBox),
	m_confidence(rhs.m_confidence),
	m_data(0),
	m_dataLength(rhs.m_dataLength)
{
	if(!rhs.m_data) return;
	
	m_data = new char[m_dataLength + 1];
	memcpy(m_data, rhs.m_data, m_dataLength);
	m_data[m_dataLength] = 0;
}

Camera::Object::~Object()
{
	delete[] m_data;
}

const Point2<unsigned> &Camera::Object::centroid() const
{
	return m_centroid;
}

const Rect<unsigned> &Camera::Object::boundingBox() const
{
	return m_boundingBox;
}

const double Camera::Object::confidence() const
{
	return m_confidence;
}

const char *Camera::Object::data() const
{
	return m_data;
}

const size_t Camera::Object::dataLength() const
{
	return m_dataLength;
}

ChannelImpl::ChannelImpl()
	: m_dirty(true)
{
}

ChannelImpl::~ChannelImpl()
{
}

void ChannelImpl::setImage(const cv::Mat &image)
{
	if(image.empty()) {
		m_image = cv::Mat();
		m_dirty = true;
		return;
	}
	m_image = image;
	m_dirty = true;
}

ObjectVector ChannelImpl::objects(const Config &config)
{
	if(m_dirty) {
		update(m_image);
		m_dirty = false;
	}
	return findObjects(config);
}

ChannelImplManager::~ChannelImplManager()
{
}

DefaultChannelImplManager::DefaultChannelImplManager()
{
	m_channelImpls["hsv"] = new Private::Camera::HsvChannelImpl();
	m_channelImpls["qr"] = new Private::Camera::BarcodeChannelImpl();
}

DefaultChannelImplManager::~DefaultChannelImplManager()
{
	std::map<std::string, ChannelImpl *>::iterator it = m_channelImpls.begin();
	for(; it != m_channelImpls.end(); ++it) delete it->second;
}

void DefaultChannelImplManager::setImage(const cv::Mat &image)
{
	std::map<std::string, ChannelImpl *>::iterator it = m_channelImpls.begin();
	for(; it != m_channelImpls.end(); ++it) it->second->setImage(image);
}

ChannelImpl *DefaultChannelImplManager::channelImpl(const std::string &name)
{
	std::map<std::string, ChannelImpl *>::iterator it = m_channelImpls.find(name);
	return (it == m_channelImpls.end()) ? 0 : it->second;
}

Camera::Channel::Channel(Device *device, const Config &config)
	: m_device(device),
	m_config(config),
	m_impl(0),
	m_valid(false)
{
	m_objects.clear();
	const std::string type = config.stringValue("type");
	if(type.empty()) {
		WARN("No type specified in config.");
		return;
	}
	
	ChannelImplManager *const manager = device->channelImplManager();
	if(manager) m_impl = manager->channelImpl(type);
	if(!m_impl) {
		WARN("Type %s not found", type.c_str());
		return;
	}
}

Camera::Channel::~Channel()
{
}

void Camera::Channel::invalidate()
{
	m_valid = false;
}

struct AreaComparator
{
public:
	bool operator()(const Camera::Object &left, const Camera::Object &right)
	{
		return left.boundingBox().area() > right.boundingBox().area();
	}
} LargestAreaFirst;

const ObjectVector *Camera::Channel::objects() const
{
	if(!m_impl) return 0;
	if(!m_valid) {
		m_objects.clear();
		m_objects = m_impl->objects(m_config);
		std::sort(m_objects.begin(), m_objects.end(), LargestAreaFirst);
		m_valid = true;
	}
	return &m_objects;
}

Device *Camera::Channel::device() const
{
	return m_device;
}

void Camera::Channel::setConfig(const Config &config)
{
	m_config = config;
	invalidate();
}

// ConfigPath //

std::string Camera::ConfigPath::s_path = "/etc/botui/channels/";

std::string Camera::ConfigPath::extension()
{
	return "conf";
}

void Camera::ConfigPath::setBasePath(const std::string &path)
{
	s_path = path;
	if(s_path.empty()) return;
	if(s_path[s_path.size() - 1] != '/') s_path += "/";
}

std::string Camera::ConfigPath::path(const std::string &name)
{
	if(name.empty()) return s_path;
	return s_path + name + "." + extension();
}

std::string Camera::ConfigPath::defaultPath()
{
	return s_path + "default";
}

std::string Camera::ConfigPath::defaultConfigPath()
{
	std::ifstream file;
	file.open(defaultPath().c_str());
	if(!file.is_open()) return std::string();
	std::string ret;
	std::getline(file, ret);
	file.close();
	return ret;
}

void Camera::ConfigPath::setDefaultConfigPath(const std::string &name)
{
	std::ofstream file;
	file.open(defaultPath().c_str());
	if(!file.is_open()) return;
	const std::string path = ConfigPath::path(name);
	file.write(path.c_str(), path.size());
	file.close();	
}

// Input Providers //

InputProvider::~InputProvider()
{
}

UsbInputProvider::UsbInputProvider()
	: m_capture(new cv::VideoCapture)
{
	setWidth(160);
	setHeight(120);
}

UsbInputProvider::~UsbInputProvider()
{
	delete m_capture;
}

bool UsbInputProvider::open(const int number)
{
	if(!m_capture || m_capture->isOpened()) return false;
	return m_capture->open(number);
}

bool UsbInputProvider::isOpen() const
{
	return m_capture->isOpened();
}

void UsbInputProvider::setWidth(const unsigned width)
{
#if CV_VERSION_EPOCH == 3
  m_capture->set(cv::CAP_PROP_FRAME_WIDTH, width);
#else
  m_capture->set(CV_CAP_PROP_FRAME_WIDTH, width);
#endif
}

void UsbInputProvider::setHeight(const unsigned height)
{
#if CV_VERSION_EPOCH == 3
  m_capture->set(cv::CAP_PROP_FRAME_HEIGHT, height);
#else
  m_capture->set(CV_CAP_PROP_FRAME_HEIGHT, height);
#endif
}

bool UsbInputProvider::next(cv::Mat &image)
{
	bool success = true;
	success &= m_capture->grab();
	success &= m_capture->retrieve(image);
	return success;
}

bool UsbInputProvider::close()
{
	if(!m_capture || !m_capture->isOpened()) return false;
	m_capture->release();
	return true;
}


using namespace depth;

static bool s_lookupTableInited = false;
static QColor s_lookupTable[350];

DepthInputProvider::DepthInputProvider(){
	if(!s_lookupTableInited){
		for(unsigned short i = 0; i < 330; i++){
			s_lookupTable[i] = QColor::fromHsv(i, 255, 255);
		}
		s_lookupTable[0] = QColor::fromRgb(0, 0, 0);
		s_lookupTableInited = true;
	}
}

DepthInputProvider::~DepthInputProvider(){
	 DepthDriver::instance().close();
}

bool DepthInputProvider::open(const int number){
	try {
      		DepthDriver::instance().open();	
    	} catch(...) {}	
	return DepthDriver::instance().isOpen();	
}

bool DepthInputProvider::isOpen() const{
	return DepthDriver::instance().isOpen();
}

void DepthInputProvider::setWidth(const unsigned width){
}

void DepthInputProvider::setHeight(const unsigned height){
}

bool DepthInputProvider::next(cv::Mat &image){
	//where magic happens
	if(!DepthDriver::instance().isOpen()){
		try {
                	DepthDriver::instance().open();
        	} catch(...) {}
		if(!DepthDriver::instance().isOpen()){
			return false;
		}
	}
	else{	
		DepthImage* depthImage = DepthDriver::instance().depthImage();
		//cv::Mat img(240, 320, CV_8UC3);
		cv::Mat img(120, 160, CV_8UC3);
		//image.create(240, 320, CV_8UC3);
		image.create(120, 160, CV_8UC3);
		for(int row = 0; row < img.rows; ++row) {
			cv::Vec3b* p = img.ptr<cv::Vec3b>(row);
			for(int col = 0; col < img.cols; ++col) {
				int32_t depth = (int32_t)depthImage->depthAt(row * 2, col * 2) - 500;
				int32_t hsv = qMax(qMin(330, (depth * 330) >> 12), 0);
				p[col][0] = s_lookupTable[hsv].blue();
				p[col][1] = s_lookupTable[hsv].red();
				p[col][2] = s_lookupTable[hsv].green();
			}
		}
		/*cv::MatIterator_<cv::Vec3b> it, end;
            	for( it = image.begin<cv::Vec3b>(), end = image.end<cv::Vec3b>(); it != end; ++it)
            	{
                	(*it)[0] = 200;
                	(*it)[1] = 20;
                	(*it)[2] = 100;
            	}*/
		image = img.clone();
	//
	}
	return true;
}

bool DepthInputProvider::close(){
	DepthDriver::instance().close();
	return ! DepthDriver::instance().isOpen();
}

// Device //

	Camera::Device::Device(InputProvider *const inputProvider)
: m_inputProvider(inputProvider),
	m_channelImplManager(new DefaultChannelImplManager),
	m_bgr(0),
	m_image(320, 240, CV_8UC3),
	m_bgrSize(0)
{
	Config *config = Config::load(Camera::ConfigPath::defaultConfigPath());
	if(!config) return;
	setConfig(*config);
	delete config;
}

Camera::Device::~Device()
{
	ChannelPtrVector::const_iterator it = m_channels.begin();
	for(; it != m_channels.end(); ++it) delete *it;
	delete m_inputProvider;
	delete m_bgr;
}

bool Camera::Device::open(const int number)
{
	if(!m_inputProvider) return false;
	return m_inputProvider->open(number);
}

bool Camera::Device::isOpen() const
{
	return m_inputProvider->isOpen();
}

void Camera::Device::setWidth(const unsigned width)
{
	m_inputProvider->setWidth(width);
}

void Camera::Device::setHeight(const unsigned height)
{
	m_inputProvider->setHeight(height);
}

unsigned Camera::Device::width() const
{
	if(m_image.empty()) return 0;
	return m_image.cols;
}

unsigned Camera::Device::height() const
{
	if(m_image.empty()) return 0;
	return m_image.rows;
}

bool Camera::Device::close()
{
	return m_inputProvider->close();
}

bool Camera::Device::update()
{
	// Get new image
	if(!m_inputProvider->next(m_image)) {
		m_image = cv::Mat();
		return false;
	}
	
	// No need to update channels if there are none.
	if(m_channels.empty()) return true;
	
	// Dirty all channel impls
	m_channelImplManager->setImage(m_image);
	
	// Invalidate all channels
	ChannelPtrVector::const_iterator it = m_channels.begin();
	for(; it != m_channels.end(); ++it) (*it)->invalidate();
	return true;
}

const ChannelPtrVector &Camera::Device::channels() const
{
	return m_channels;
}

InputProvider *Camera::Device::inputProvider() const
{
	return m_inputProvider;
}

const cv::Mat &Camera::Device::rawImage() const
{
	return m_image;
}

void Camera::Device::setConfig(const Config &config)
{
	m_config = config;
	updateConfig();
}

const Config &Camera::Device::config() const
{
	return m_config;
}

void Camera::Device::setChannelImplManager(ChannelImplManager *channelImplManager)
{
	delete m_channelImplManager;
	m_channelImplManager = channelImplManager;
}

ChannelImplManager *Camera::Device::channelImplManager() const
{
	return m_channelImplManager;
}

const unsigned char *Camera::Device::bgr() const
{
	const unsigned correctSize = m_image.rows * m_image.cols * m_image.elemSize();
	if(m_bgrSize != correctSize) {
		delete m_bgr;
		m_bgrSize = correctSize;
		m_bgr = new unsigned char[m_bgrSize];
	}
	
	for(unsigned row = 0; row < m_image.rows; ++row) {
		unsigned offset1 = row * m_image.cols * m_image.elemSize();
		memcpy(m_bgr + offset1, m_image.ptr(row), m_image.cols * m_image.elemSize());
	}
	
	return m_bgr;
}

void Camera::Device::updateConfig()
{
	ChannelPtrVector::const_iterator it = m_channels.begin();
	for(; it != m_channels.end(); ++it) delete *it;
	m_channels.clear();
	
	m_config.clearGroup();
	m_config.beginGroup(CAMERA_GROUP);
	int numChannels = m_config.intValue(CAMERA_NUM_CHANNELS_KEY);
	if(numChannels <= 0) return;
	for(int i = 0; i < numChannels; ++i) {
		std::stringstream stream;
		stream << CAMERA_CHANNEL_GROUP_PREFIX;
		stream << i;
		m_config.beginGroup(stream.str());
		m_channels.push_back(new Channel(this, m_config));
		m_config.endGroup();
	}
	m_config.endGroup();
}

Camera::Device *Camera::cDevice()
{
	return Private::DeviceSingleton::instance();
}
