#include "ukf.h"
#include "tools.h"
#include "Eigen/Dense"
#include <iostream>
#include <math.h>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF() {

  //initialized or not
  is_initialized_ = false;

  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  // initial state vector
  x_ = VectorXd(5);

  // initial covariance matrix
  P_ = MatrixXd(5, 5);
  P_ << 1, 0, 0, 0, 0,
      0, 1, 0, 0, 0,
      0, 0, 1, 0, 0,
      0, 0, 0, 1, 0,
      0, 0, 0, 0, 1;

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = .5;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = .5;

  // Laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // Laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;

  //set state dimension
  n_x_ = 5;

  //set measurement dimension, radar can measure r, phi, and r_dot
  n_z_ = 3;

  //set augmented dimension
  n_aug_= 7;

  //* Sigma point spreading parameter
  lambda_ = 1;


  //create vector for weights
  weights_ = VectorXd(2*n_aug_+1);

  // set weights
  double weight_0 = lambda_/(lambda_+n_aug_);
  weights_(0) = weight_0;
  for (int i=1; i<2*n_aug_+1; i++) {  //2n+1 weights
    double weight = 0.5/(n_aug_+lambda_);
    weights_(i) = weight;
  }

  //add measurement noise covariance matrix
  R_r_ = MatrixXd(3,3);
  R_r_ <<    std_radr_*std_radr_, 0, 0,
      0, std_radphi_*std_radphi_, 0,
      0, 0,std_radrd_*std_radrd_;

  R_l_ = MatrixXd(2, 2);
  R_l_ <<    std_laspx_*std_laspx_, 0,
      0, std_laspy_*std_laspy_;

  //measurement matrix
  H_ = MatrixXd(2, 5);
  H_ << 1, 0, 0, 0, 0,
        0, 1, 0, 0, 0;


  Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

  NIS_laser_ = 0.;

  NIS_radar_ = 0.;


  /**
  TODO:

  Complete the initialization. See ukf.h for other member properties.

  Hint: one or more values initialized above might be wildly off...
  */
}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Make sure you switch between lidar and radar
  measurements.
  */
  if (!is_initialized_) {
    // first measurement
    x_.fill(0.0);
    time_us_ = meas_package.timestamp_;


    if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
      /**
      Convert radar from polar to cartesian coordinates and initialize state.
      */
      x_ << meas_package.raw_measurements_(0) * cos(meas_package.raw_measurements_(1)),
          meas_package.raw_measurements_(0) * sin(meas_package.raw_measurements_(1)), 0, 0, 0;

    }
    else if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
      /**
      Initialize state.
      */
      x_ << meas_package.raw_measurements_(0),
          meas_package.raw_measurements_(1), 0, 0, 0;
    }
    is_initialized_ = true;
    return;
  }

  double dt = (meas_package.timestamp_ - time_us_) / 1000000.0;	//dt - expressed in seconds
  time_us_ = meas_package.timestamp_;

  //prediction step
  Prediction(dt);

  if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
    if (!use_laser_) { return; }
    UpdateLidar(meas_package);
  }
  else if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
    if (!use_radar_) { return; }
    UpdateRadar(meas_package);
  }
}


/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t) {

  /**
  TODO:

  Complete this function! Estimate the object's location. Modify the state
  vector, x_. Predict sigma points, the state, and the state covariance matrix.
  */

  //create augmented state vector
  VectorXd x_aug = VectorXd(7);
  x_aug.head(5) = x_;
  x_aug(5) = 0;
  x_aug(6) = 0;


  //create augmented covariance matrix
  MatrixXd P_aug = MatrixXd(7, 7);
  P_aug.fill(0.0);
  P_aug.topLeftCorner(5,5) = P_;
  P_aug(5,5) = std_a_*std_a_;
  P_aug(6,6) = std_yawdd_*std_yawdd_;

  //create square root matrix
  MatrixXd L = P_aug.llt().matrixL();

  //create augmented sigma points
  //create sigma point matrix
  MatrixXd Xsig_aug = MatrixXd(n_aug_, 2 * n_aug_ + 1);
  Xsig_aug.fill(0.0);

  Xsig_aug.col(0)  = x_aug;
  for (int i = 0; i< n_aug_; i++)
  {
    Xsig_aug.col(i+1)       = x_aug + sqrt(lambda_+n_aug_) * L.col(i);
    Xsig_aug.col(i+1+n_aug_) = x_aug - sqrt(lambda_+n_aug_) * L.col(i);
  }

  //predict sigma point
  Xsig_pred_.fill(0.0);

  for (int i = 0; i< 2*n_aug_+1; i++) {
    //extract values for better readability
    double p_x = Xsig_aug(0, i);
    double p_y = Xsig_aug(1, i);
    double v = Xsig_aug(2, i);
    double yaw = Xsig_aug(3, i);
    double yawd = Xsig_aug(4, i);
    double nu_a = Xsig_aug(5, i);
    double nu_yawdd = Xsig_aug(6, i);

    //predicted state values
    double px_p, py_p;

    //avoid division by zero
    if (fabs(yawd) > 0.001) {
      px_p = p_x + v / yawd * (sin(yaw + yawd * delta_t) - sin(yaw));
      py_p = p_y + v / yawd * (cos(yaw) - cos(yaw + yawd * delta_t));
    } else {
      px_p = p_x + v * delta_t * cos(yaw);
      py_p = p_y + v * delta_t * sin(yaw);
    }

    double v_p = v;
    double yaw_p = yaw + yawd * delta_t;
    double yawd_p = yawd;

    //add noise
    px_p = px_p + 0.5 * nu_a * delta_t * delta_t * cos(yaw);
    py_p = py_p + 0.5 * nu_a * delta_t * delta_t * sin(yaw);
    v_p = v_p + nu_a * delta_t;

    yaw_p = yaw_p + 0.5 * nu_yawdd * delta_t * delta_t;
    yawd_p = yawd_p + nu_yawdd * delta_t;
    //write predicted sigma point into right column
    //create matrix with predicted sigma points as columns

    Xsig_pred_(0, i) = px_p;
    Xsig_pred_(1, i) = py_p;
    Xsig_pred_(2, i) = v_p;
    Xsig_pred_(3, i) = yaw_p;
    Xsig_pred_(4, i) = yawd_p;

  }
  //predicted state mean
  x_.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //iterate over sigma points
    x_ = x_+ weights_(i) * Xsig_pred_.col(i);
  }

  //predicted state covariance matrix
  P_.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //iterate over sigma points
    // state difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    //angle normalization
    x_diff(3) = atan2(sin(x_diff(3)),cos(x_diff(3)));
    P_ = P_ + weights_(i) * x_diff * x_diff.transpose() ;
  }
}

/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Use lidar data to update the belief about the object's
  position. Modify the state vector, x_, and covariance, P_.

  You'll also need to calculate the lidar NIS.
  */



  MatrixXd z_sig = H_ * Xsig_pred_;
  MatrixXd z_pred = VectorXd(2);
  z_pred.fill(0.0);
  for (int i=0; i < 2*n_aug_+1; i++) {
    z_pred = z_pred + weights_(i) * z_sig.col(i);
  }
  //measurement covariance matrix S
  MatrixXd S_l = MatrixXd(2, 2);
  S_l.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points
    //residual
    VectorXd z_diff = z_sig.col(i) - z_pred;
    S_l = S_l + weights_(i) * z_diff * z_diff.transpose();
  }
  S_l = S_l + R_l_;


  //create matrix for cross correlation Tc
  MatrixXd Tc_l = MatrixXd(n_x_, 2);
  Tc_l.fill(0.0);
  //calculate cross correlation matrix

  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points

    //residual
    VectorXd z_diff = z_sig.col(i) - z_pred;

    // state difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    //angle normalization
    x_diff(3) = atan2(sin(x_diff(3)),cos(x_diff(3)));
    Tc_l = Tc_l + weights_(i) * x_diff * z_diff.transpose();
  }

  //Kalman gain K;
  MatrixXd K = Tc_l * S_l.inverse();

  //Measurement
  VectorXd z_ = meas_package.raw_measurements_;

  //residual
  VectorXd z_diff = z_ - z_pred;

  //update state mean and covariance matrix
  x_ = x_ + K * z_diff;
  x_(3) = atan2(sin(x_(3)),cos(x_(3)));
  P_ = P_ - K*S_l*K.transpose();

  NIS_laser_ = z_diff.transpose()*S_l.inverse()*z_diff;
}

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Use radar data to update the belief about the object's
  position. Modify the state vector, x_, and covariance, P_.

  You'll also need to calculate the radar NIS.
  */
  MatrixXd Zsig = MatrixXd(n_z_, 2 * n_aug_ + 1);
  Zsig.fill(0.0);
  //transform sigma points into measurement space
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points

    // extract values for better readibility
    double p_x = Xsig_pred_(0,i);
    double p_y = Xsig_pred_(1,i);
    double v  = Xsig_pred_(2,i);
    double yaw = Xsig_pred_(3,i);

    double v1 = cos(yaw)*v;
    double v2 = sin(yaw)*v;

    // measurement model
    Zsig(0,i) = sqrt(p_x*p_x + p_y*p_y);                        //r
    Zsig(1,i) = atan2(p_y,p_x);                                 //phi
    Zsig(2,i) = (p_x*v1 + p_y*v2 ) / sqrt(p_x*p_x + p_y*p_y);   //r_dot
  }


  //mean predicted measurement
  VectorXd z_pred = VectorXd(n_z_);
  z_pred.fill(0.0);
  for (int i=0; i < 2*n_aug_+1; i++) {
    z_pred = z_pred + weights_(i) * Zsig.col(i);
  }

  //measurement covariance matrix S
  MatrixXd S_r = MatrixXd(n_z_,n_z_);
  S_r.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points
    //residual
    VectorXd z_diff = Zsig.col(i) - z_pred;

    //angle normalization
    z_diff(1) = atan2(sin(z_diff(1)),cos(z_diff(1)));
    S_r = S_r + weights_(i) * z_diff * z_diff.transpose();
  }

  S_r = S_r + R_r_;


  //create matrix for cross correlation Tc
  MatrixXd Tc_r = MatrixXd(n_x_, n_z_);
  Tc_r.fill(0.0);
  //calculate cross correlation matrix
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points

    //residual
    VectorXd z_diff = Zsig.col(i) - z_pred;
    //angle normalization
    z_diff(1) = atan2(sin(z_diff(1)),cos(z_diff(1)));
    // state difference
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    //angle normalization
    x_diff(3) = atan2(sin(x_diff(3)),cos(x_diff(3)));
    Tc_r = Tc_r + weights_(i) * x_diff * z_diff.transpose();
  }

  //Kalman gain K;
  MatrixXd K = Tc_r * S_r.inverse();

  //Measurement
  VectorXd z_ = meas_package.raw_measurements_;

  //residual
  VectorXd z_diff = z_ - z_pred;

  //angle normalization
  z_diff(1) = atan2(sin(z_diff(1)),cos(z_diff(1)));

  //update state mean and covariance matrix
  x_ = x_ + K * z_diff;
  x_(3) = atan2(sin(x_(3)),cos(x_(3)));
  P_ = P_ - K*S_r*K.transpose();

  NIS_radar_ = z_diff.transpose()*S_r.inverse()*z_diff;
}
