#include <iostream>
#include <sstream>

// NuiApi.hの前にWindows.hをインクルードする
#include <Windows.h>
#include <NuiApi.h>

#include <opencv2/opencv.hpp>



#define ERROR_CHECK( ret )  \
  if ( ret != S_OK ) {    \
    std::stringstream ss;	\
    ss << "failed " #ret " " << std::hex << ret << std::endl;			\
    throw std::runtime_error( ss.str().c_str() );			\
  }

const NUI_IMAGE_RESOLUTION CAMERA_RESOLUTION = NUI_IMAGE_RESOLUTION_640x480;

class KinectSample
{
private:

  INuiSensor* kinect;
  HANDLE imageStreamHandle;
  HANDLE depthStreamHandle;
	HANDLE streamEvent;

  DWORD width;
  DWORD height;

public:

  KinectSample()
  {
  }

  ~KinectSample()
  {
    // 終了処理
    if ( kinect != 0 ) {
      kinect->NuiShutdown();
      kinect->Release();
    }
  }

  void initialize()
  {
    createInstance();

    // Kinectの設定を初期化する
    ERROR_CHECK( kinect->NuiInitialize( NUI_INITIALIZE_FLAG_USES_COLOR | NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | NUI_INITIALIZE_FLAG_USES_SKELETON ) );

    // RGBカメラを初期化する
    ERROR_CHECK( kinect->NuiImageStreamOpen( NUI_IMAGE_TYPE_COLOR, CAMERA_RESOLUTION,
      0, 2, 0, &imageStreamHandle ) );

    // 距離カメラを初期化する
    ERROR_CHECK( kinect->NuiImageStreamOpen( NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX, CAMERA_RESOLUTION,
      0, 2, 0, &depthStreamHandle ) );

		// 距離カメラを初期化する(Nearモード)
    //ERROR_CHECK( kinect->NuiImageStreamOpen( NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX, CAMERA_RESOLUTION,
    //  NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE, 2, 0, &depthStreamHandle ) );

		// スケルトンを初期化する
    ERROR_CHECK( kinect->NuiSkeletonTrackingEnable( 0, NUI_SKELETON_TRACKING_FLAG_SUPPRESS_NO_FRAME_DATA ) );

    // フレーム更新イベントのハンドルを作成する
    streamEvent = ::CreateEvent( 0, TRUE, FALSE, 0 );
    ERROR_CHECK( kinect->NuiSetFrameEndEvent( streamEvent, 0 ) );

    // 指定した解像度の、画面サイズを取得する
    ::NuiImageResolutionToSize(CAMERA_RESOLUTION, width, height );
  }

  void run()
  {
    cv::Mat image;

    // メインループ
    while ( 1 ) {
      // データの更新を待つ
      DWORD ret = ::WaitForSingleObject( streamEvent, INFINITE );
      ::ResetEvent( streamEvent );

      drawRgbImage( image );
      drawDepthImage( image );
      drawSkeleton( image );

      // 画像を表示する
      cv::imshow( "KinectSample", image );

      // キー入力チェック兼、表示のためのウェイト
      int key = cv::waitKey( 10 );
      if ( key == 'q' ) {
        break;
      }
			// 上キー
			else if ( (key >> 16) == VK_UP ) {
				LONG angle = 0;
				kinect->NuiCameraElevationGetAngle( &angle );

				angle = std::min( angle + 5, (LONG)NUI_CAMERA_ELEVATION_MAXIMUM );
				kinect->NuiCameraElevationSetAngle( angle );
			}
			// 下キー
			else if ( (key >> 16) == VK_DOWN ) {
				LONG angle = 0;
				kinect->NuiCameraElevationGetAngle( &angle );

				angle = std::max( angle - 5, (LONG)NUI_CAMERA_ELEVATION_MINIMUM );
				kinect->NuiCameraElevationSetAngle( angle );
			}
		}
  }

private:

  void createInstance()
  {
    // 接続されているKinectの数を取得する
    int count = 0;
    ERROR_CHECK( ::NuiGetSensorCount( &count ) );
    if ( count == 0 ) {
      throw std::runtime_error( "Kinect を接続してください" );
    }

    // 最初のKinectのインスタンスを作成する
    ERROR_CHECK( ::NuiCreateSensorByIndex( 0, &kinect ) );

    // Kinectの状態を取得する
    HRESULT status = kinect->NuiStatus();
    if ( status != S_OK ) {
      throw std::runtime_error( "Kinect が利用可能ではありません" );
    }
  }

  void drawRgbImage( cv::Mat& image )
  {
      // RGBカメラのフレームデータを取得する
      NUI_IMAGE_FRAME imageFrame = { 0 };
      ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( imageStreamHandle, INFINITE, &imageFrame ) );

      // 画像データを取得する
      NUI_LOCKED_RECT colorData;
      imageFrame.pFrameTexture->LockRect( 0, &colorData, 0, 0 );

      // 画像データをコピーする
      image = cv::Mat( height, width, CV_8UC4, colorData.pBits );

      // フレームデータを解放する
      ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( imageStreamHandle, &imageFrame ) );
  }

  void drawDepthImage( cv::Mat& image )
  {
      // 距離カメラのフレームデータを取得する
      NUI_IMAGE_FRAME depthFrame = { 0 };
      ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( depthStreamHandle, 0, &depthFrame ) );

      // 距離データを取得する
      NUI_LOCKED_RECT depthData = { 0 };
      depthFrame.pFrameTexture->LockRect( 0, &depthData, 0, 0 );

      USHORT* depth = (USHORT*)depthData.pBits;
      for ( int i = 0; i < (depthData.size / sizeof(USHORT)); ++i ) {
        USHORT distance = ::NuiDepthPixelToDepth( depth[i] );
        USHORT player = ::NuiDepthPixelToPlayerIndex( depth[i] );

        LONG depthX = i % width;
        LONG depthY = i / width;
        LONG colorX = 0;
        LONG colorY = 0;

        // 距離カメラの座標を、RGBカメラの座標に変換する
        kinect->NuiImageGetColorPixelCoordinatesFromDepthPixelAtResolution(
          CAMERA_RESOLUTION, CAMERA_RESOLUTION,
          0, depthX , depthY, depth[i], &colorX, &colorY );

				// プレイヤー
        if ( player != 0 ) {
            int index = ((colorY * width) + colorX) * 4;
            UCHAR* data = &image.data[index];
            data[0] = 0;
            data[1] = 0;
            data[2] = 255;
        }
        else {
	        // 一定以上の距離を描画しない
          if ( distance >= 1000 ) {
            int index = ((colorY * width) + colorX) * 4;
            UCHAR* data = &image.data[index];
            data[0] = 255;
            data[1] = 255;
            data[2] = 255;
          }
        }
      }

      // フレームデータを解放する
      ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( depthStreamHandle, &depthFrame ) );
  }

	void drawSkeleton( cv::Mat& image )
	{
		bool isFindFirstSkeleton = false;

		// スケルトンのフレームを取得する
    NUI_SKELETON_FRAME skeletonFrame = { 0 };
    kinect->NuiSkeletonGetNextFrame( 0, &skeletonFrame );
    //ERROR_CHECK( kinect->NuiSkeletonGetNextFrame( 0, &skeletonFrame ) );
    for ( int i = 0; i < 6; ++i ) {
      NUI_SKELETON_DATA& skeletonData = skeletonFrame.SkeletonData[i];
      if ( skeletonData.eTrackingState == NUI_SKELETON_TRACKED ) {
        for ( int j = 0; j < 20; ++j ) {
          if ( skeletonData.eSkeletonPositionTrackingState[j] != NUI_SKELETON_POSITION_NOT_TRACKED ) {
						drawJoint( image, skeletonData.SkeletonPositions[j] );
          }
        }
      }
      else if ( skeletonData.eTrackingState == NUI_SKELETON_POSITION_ONLY ) {
				drawJoint( image, skeletonData.Position );
      }

    }
	}

	void drawJoint( cv::Mat& image, Vector4 position )
	{
    FLOAT depthX = 0, depthY = 0;
    ::NuiTransformSkeletonToDepthImage( position, &depthX, &depthY, CAMERA_RESOLUTION );

    LONG colorX = 0;
    LONG colorY = 0;

    kinect->NuiImageGetColorPixelCoordinatesFromDepthPixelAtResolution(
      CAMERA_RESOLUTION, CAMERA_RESOLUTION,
      0, (LONG)depthX , (LONG)depthY, 0, &colorX, &colorY );

    cv::circle( image, cv::Point( colorX, colorY ), 10, cv::Scalar( 0, 255, 0 ), 5 );
	}
};

void main()
{

  try {
    KinectSample kinect;
    kinect.initialize();
    kinect.run();
  }
  catch ( std::exception& ex ) {
    std::cout << ex.what() << std::endl;
  }
}
