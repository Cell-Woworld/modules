/*
*********************************************************************
http://www.mysqltutorial.org
*********************************************************************
name: MySQL Database JustDoIt
Link: http://www.woworld.com
Version 1.0
+ initial commit 
*********************************************************************
*/


/*!40101 SET NAMES utf8mb4 */;

/*!40101 SET SQL_MODE=''*/;

/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;
CREATE DATABASE /*!32312 IF NOT EXISTS*/`JustDoIt` /*!40100 DEFAULT CHARACTER SET utf8mb4 */;



/* Dependent table.column
1. User.userId
*/


/*Table structure for table `Punch` */

DROP TABLE IF EXISTS `Punch`;

CREATE TABLE `Punch` (
  `id` int NOT NULL AUTO_INCREMENT,
  `userId` VARBINARY(16) NOT NULL,
  `punchTime` datetime NOT NULL,
  `punchType` VARCHAR(1) NOT NULL,
  `latitude` DECIMAL(11, 6) NOT NULL,
  `longitude` DECIMAL(11, 6) NOT NULL,
  PRIMARY KEY (`id`),
  CONSTRAINT `PunchIbfk_1` FOREIGN KEY (`userId`) REFERENCES `User` (`userId`),
  KEY `idx_Punch_userId_punchTime_punchType` (`userId`,`punchTime`,`punchType`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;


/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;
