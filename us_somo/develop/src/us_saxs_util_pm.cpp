#include "../include/us_saxs_util.h"
#include "../include/us_file_util.h"
#include "../include/us_pm.h"
#include "../include/us_timer.h"
#ifdef WIN32
# include <sys/timeb.h>
#else
# include <sys/time.h>
//Added by qt3to4:
#include <QTextStream>
#endif

QString US_Saxs_Util::run_json( QString & json )
{
   // note: alternate version for mpi in in run_json_mpi() in us_saxs_util_pm_mpi.cpp

   if ( !us_log )
   {
      us_log = new US_Log( "runlog.txt" );
      // us_log->log( "initial json" );
      // us_log->log( json );
   }

   us_log->datetime( "start run_json" );

   map < QString, QString > parameters = US_Json::split( json );
   map < QString, QString > results;

   for ( map < QString, QString >::iterator it = parameters.begin();
         it != parameters.end();
         ++it )
   {
      // us_qdebug( QString( "%1 : %2" ).arg( it->first ).arg( it->second ) );
      if ( it->first.left( 1 ) == "_" )
      {
         results[ it->first ] = it->second;
      }
   }

    
   if ( parameters.count( "_udphost" ) &&
        parameters.count( "_udpport" ) &&
	parameters.count( "_uuid" ) )
   {
     map < QString, QString > msging;
      msging[ "_uuid" ] = results[ "_uuid" ];
      //msging[ "status" ] = results[ "_textarea" ];
      us_udp_msg = new US_Udp_Msg( parameters[ "_udphost" ], (quint16) parameters[ "_udpport" ].toUInt() );
      us_udp_msg->set_default_json( msging );
   }

   {
      QStringList supported;
      supported
         << "pmrun"
	 << "hydro"
	 << "pat"
	;
      
      int count = 0;
      for ( int i = 0; i < (int) supported.size(); ++i )
	{
          if ( parameters.count( supported[ i ] ) )
	    {
	      count++;
	    }
	}
      
      if ( !count )
	{
          results[ "errors" ] = "no supported runtype found in input json";
          return US_Json::compose( results );
	}
      if ( count > 1 )
	{
	  results[ "errors" ] = "only one run type currently allowed per input json";
	  return US_Json::compose( results );
	}
       
   }
   
   if ( parameters.count( "pmrun" ) )
     {
       if ( !run_pm( parameters, results ) )
	 {
	   results[ "errors" ] += " run_pm failed:" + errormsg;
	   //return US_Json::compose( results );

	 }
     }
 
   if ( parameters.count( "hydro" ) )
     {
       if ( !run_hydro( parameters, results ) )
	 {
	   results[ "errors" ] += " run_hydro failed:" + errormsg;
	   //return US_Json::compose( results );
	 }
     }
   
   if ( parameters.count( "pat" ) )
     {
       if ( !run_pat( parameters, results ) )
	 {
	   results[ "errors" ] += " pat failed:" + errormsg;
	   //return US_Json::compose( results );
	 }
     }
   
   
   // if ( us_log )
   // {
   //    us_log->log( "final json" );
   //    us_log->log( US_Json::compose( results ) );
   //    us_log->datetime( "end run_json" );
   // }
   return US_Json::compose( results );
}

#if !defined(CMDLINE) 
bool US_Saxs_Util::run_hydro(
			     map < QString, QString >           & ,
			     map < QString, QString >           & 
			     )
{
   return false;
}

void US_Saxs_Util::read_residue_file() {};
bool US_Saxs_Util::screen_pdb(QString, bool) { return false; };
bool US_Saxs_Util::set_default(map < QString, QString > & , map < QString, QString > & ) { return false; };

#endif

bool US_Saxs_Util::run_pm(
                          map < QString, QString >           & parameters,
                          map < QString, QString >           & results
                          )
{
   if ( !parameters.count( "pmfiles" ) )
   {
      results[ "errors" ] = "no pmfiles specified";
      return false;
   }

   parameters[ "pmfiles" ].replace( "\\/", "/" ).replace( QRegExp( "^\"" ), "" ).replace( QRegExp( "\"$" ), "" );
   QStringList files;

   {
      QStringList files_try;
      {
         QString qs = "\",\"";
         files_try = (parameters[ "pmfiles" ] ).split( qs , QString::SkipEmptyParts );
      }
      for ( int i = 0; i < (int) files_try.size(); ++i )
      {
         // us_qdebug( QString( "file %1" ).arg( files_try[ i ] ) );
         QFileInfo fi( files_try[ i ] );
         if ( !fi.exists() )
         {
            results[ "errors" ] += QString( " file %1 does not exist." ).arg( files_try[ i ] );
         } else {
            if ( !fi.isReadable() )
            {
               results[ "errors" ] += QString( " file %1 exists but is not readable." ).arg( files_try[ i ] );
            } else {
               files << files_try[ i ];
            }
         }
      }
   }

   if ( !files.size() )
   {
      return false;
   }

   QStringList     models_written;
   QStringList     dats_written;

   QString         base_dir;
   if ( parameters.count( "_base_directory" ) )
   {
      base_dir = parameters[ "_base_directory" ] + QDir::separator();
      base_dir.replace( "\\/", "/" );
   }

   for ( int i = 0; i < (int) files.size(); ++i )
   {
      if ( us_udp_msg )
      {
         map < QString, QString > msging;
         msging[ "status" ] = QString( "processing %1" ).arg( QFileInfo( files[ i ] ).fileName() );
         us_udp_msg->send_json( msging );
      }

      map < QString, vector < double > > vectors;

      if ( !read_sas_data( files[ i ],
                           vectors[ "pmq" ],
                           vectors[ "pmi" ],
                           vectors[ "pme" ],
                           errormsg ) )
      {
         results[ "errors" ] += " " + errormsg;
         continue;
      }

      double pmminq = parameters.count( "pmminq" ) ? parameters[ "pmminq" ].toDouble() : 0e0;
      double pmmaxq = parameters.count( "pmmaxq" ) ? parameters[ "pmmaxq" ].toDouble() : 7e0;
      int pmqpoints = parameters.count( "pmqpoints" ) ? parameters[ "pmqpoints" ].toInt() : 5000;
      bool pmlogbin = parameters.count( "pmlogbin" );

      clip_data( pmminq, 
                 pmmaxq, 
                 vectors[ "pmq" ], 
                 vectors[ "pmi" ],
                 vectors[ "pme" ] );

      if ( vectors[ "pmq" ].size() < 3 )
      {
         results[ "errors" ] += QString( " after q range cropping %1 had less then 3 points of data left" ).arg( files[ i ] );
         continue;
      }

      errormsg  = "";
      noticemsg = "";
      
      bin_data( pmqpoints, 
                pmlogbin, 
                vectors[ "pmq" ], 
                vectors[ "pmi" ],
                vectors[ "pme" ],
                errormsg, 
                noticemsg );

      // if ( !noticemsg.isEmpty() )
      // {
      //    results[ "notice" ] += " " + noticemsg;
      // }
      if ( !errormsg.isEmpty() )
      {
         results[ "errors" ] += " " + errormsg;
         continue;
      }

      if ( vectors[ "pmq" ].size() < 3 )
      {
         results[ "errors" ] += QString( " after binning %1 had less then 3 points of data left" ).arg( files[ i ] );
         continue;
      }
      
      map < QString, vector < double > > produced_q;
      map < QString, vector < double > > produced_I;
      map < QString, QString >           produced_model;

      parameters[ "pmoutname" ] = QFileInfo( files[ i ] ).completeBaseName();

      if ( !run_pm( produced_q,
                    produced_I,
                    produced_model,
                    parameters,
                    vectors,
                    false ) )
      {
         results[ "errors" ] += QString( " run_pm error %1: %2" ).arg( files[ i ] ).arg( errormsg );
         continue;
      }

      for ( map < QString, vector < double > >::iterator it = produced_q.begin();
            it != produced_q.end();
            ++it )
      {
         QString name = it->first;
         QString msg;
         if ( !write_iq( name, msg, it->second, produced_I[ it->first ] ) )
         {
            results[ "errors" ] += QString( " write error: %1" ).arg( msg );
         } else {
            dats_written << base_dir + name;
         }

         if ( !US_File_Util::writeuniq( name, msg, it->first, "bead_model", produced_model[ it->first ] ) )
         {
            results[ "errors" ] += QString( " write error: %1" ).arg( msg );
         } else {
            models_written << base_dir + name;
         }
      }
      if ( us_udp_msg )
      {
	map < QString, QString > msging;
         msging[ "status" ] = QString( "finished %1" ).arg( QFileInfo( files[ i ] ).fileName() );
         msging[ "models" ] = "[\"" + models_written.join( "\",\"" ) + "\"]";
         msging[ "dats"   ] = "[\"" + dats_written  .join( "\",\"" ) + "\"]";
         us_log->log( "msging json" );
         us_log->log( US_Json::compose( msging ) );
         us_udp_msg->send_json( msging );
      }
   }

   results[ "models" ] = "[\"" + models_written.join( "\",\"" ) + "\"]";
   results[ "dats"   ] = "[\"" + dats_written  .join( "\",\"" ) + "\"]";

   if ( results.count( "errors" ) &&
        !results[ "errors" ].isEmpty() )
   {
      return false;
   }
   return true;
}

static US_Timer usupm_timer;

bool US_Saxs_Util::flush_pm_csv( 
                                vector < QString >           & csv_name,
                                vector < double >            & csv_q,
                                vector < vector < double > > & csv_I 
                                )
{
   if ( !csv_name.size() )
   {
      return true;
   }

   QString filename = csv_name[ 0 ];
   {
      int ext = 0;
      QString use_filename = QString( "%1.csv" ).arg( filename );
      while( QFile::exists( use_filename ) )
      {
         use_filename = QString( "%1-%2.csv" ).arg( filename ).arg( ++ext );
      }
      filename = use_filename;
   }
   
   if ( us_log )
   {
      us_log->log( "Creating:" + filename );
   }
   QFile of( filename );
   if ( !of.open( QIODevice::WriteOnly ) )
   {
      errormsg = QString( "Error: can not open %1 for writing" ).arg( filename );
      return false;
   }

   QTextStream ts( &of );

   ts << QString( "\"Name\",\"Type; q:\",%1,\"PM models %2\"\n" )
      .arg( vector_double_to_csv( csv_q ) )
      .arg( csv_name[ 0 ] )
      ;

   for ( int i = 0; i < (int) csv_I.size(); ++i )
   {
      ts << QString( "\"%1\",\"%2\",%3\n" )
         .arg( csv_name[ i ] )
         .arg( "I(q)" )
         .arg( vector_double_to_csv( csv_I[ i ] ) )
         ;
   }

   of.close();
   output_files << filename;
   csv_name.clear( );
   csv_q   .clear( );
   csv_I   .clear( );
   return true;
}


bool US_Saxs_Util::run_pm( QString controlfile )
{
   // for now, everyone reads the control file & sets things up to the point of nsa run
   usupm_timer.init_timer ( "pm_init" );
   usupm_timer.init_timer ( "pm_run" );
   usupm_timer.start_timer ( "pm_init" );

   pm_ga_fitness_secs = 0e0;
   pm_ga_fitness_calls = 0;

   QString qs_base_dir = QDir::currentPath();

   QString outputData = QString( "%1" ).arg( getenv( "outputData" ) );
   if ( outputData.isEmpty() )
   {
      outputData = "outputData";
   }

   int errorno = -1;

   errormsg = "";

   if ( !QFile::exists( controlfile ) )
   {
      errormsg = QString( "controlfile %1 does not exist" ).arg( controlfile );
      return false;
   }         
   errorno--;

   QStringList qslt;

   QString dest = controlfile;
   int result;

   // copy here
   QDir qd1 = QDir::current();
   qd1.makeAbsolute();
   QDir qd2( QFileInfo( controlfile ).absoluteDir() );
   
   US_File_Util ufu;

   if ( qd1 != qd2 )
   {
      ufu.copy( controlfile, QDir::currentPath() + QDir::separator() + QFileInfo( controlfile ).fileName() );
      if ( us_log )
      {
         us_log->log( 
                     QString( "copying %1 %2 <%3>\n" )
                     .arg( controlfile )
                     .arg( QDir::currentPath() + QDir::separator() + QFileInfo( controlfile ).fileName() )
                     .arg( ufu.errormsg ) );
      }
      dest = QFileInfo( controlfile ).fileName();

      if ( us_log )
      {
         us_log->log( QString( "dest is now %1\n" ).arg( dest ) );
      }
   }

   bool use_tar = false;

   if ( controlfile.contains( QRegExp( "\\.(tgz|TGZ)$" ) ) )
   {
      // gunzip controlfile, must be renamed for us_gzip
      
      // rename
      dest.replace( QRegExp( "\\.(tgz|TGZ)$" ), ".tar.gz" );
      QDir qd;
      qd.remove( dest );
      if ( !qd.rename( controlfile, dest ) )
      {
         errormsg = QString("Error renaming %1 to %2\n").arg( controlfile ).arg( dest );
         return false;
      }
      errorno--;
      
      controlfile = dest;

      US_Gzip usg;
      result = usg.gunzip( controlfile );
      if ( GZIP_OK != result )
      {
         errormsg = QString("Error: %1 problem gunzipping (%2)\n").arg( controlfile ).arg( usg.explain( result ) );
         return false;
      }
      errorno--;

      controlfile.replace( QRegExp( "\\.gz$" ), "" );
   }

   QStringList qsl;

   if ( controlfile.contains( QRegExp( "\\.(tar|TAR)$" ) ) )
   {
      use_tar = true;
      // tar open controlfile
      US_Tar ust;
      result = ust.extract( controlfile, &qslt );
      if ( TAR_OK != result )
      {
         errormsg = QString("Error: %1 problem extracting tar archive (%2)\n").arg( controlfile ).arg( ust.explain( result ) );
         return false;
      }
      if ( !ufu.read( qslt[ 0 ], qsl ) )
      {
         errormsg = QString("Error: %1").arg( ufu.errormsg );
         return false;
      }
   } else {
      // open controlfile as regular text file contain commands
      if ( !ufu.read( controlfile, qsl ) )
      {
         errormsg = QString("Error: %1").arg( ufu.errormsg );
         return false;
      }
   }      

   if ( us_log )
   {
      us_log->log( QString( "control file contains:\n%1\n-------\n" ).arg( qsl.join( "\n" ) ) );
   }
   usupm_timer.end_timer ( "pm_init" );
   usupm_timer.start_timer ( "pm_run" );

   if ( !run_pm( qsl ) )
   {
      if ( us_log )
      {
         us_log->log( errormsg );
      }
   }

   usupm_timer.end_timer( "pm_run" );

   if ( us_log )
   {
      us_log->log( QString( "my output files:<%1>\ncontrolfile %2\n" )
                   .arg( output_files.join( ":" ) ) 
                   .arg( controlfile ) );
   }

   QString runinfo_base = "runinfo";
#if defined( USE_MPI )
   runinfo_base = QString( "%1-n%2" ).arg( runinfo_base ).arg( npes );
#endif

   QString runinfo = runinfo_base;
   int ext = 0;
   while ( QFile::exists( runinfo ) )
   {
      runinfo = QString( "%1-%2" ).arg( runinfo_base ).arg( ++ext );
   }

   QFile f( runinfo );
   if ( f.open( QIODevice::WriteOnly ) )
   {
      QTextStream ts( &f );
      ts << "timings:\n";
      ts << usupm_timer.list_times();;
      ts << QString( "ga fit calls %1 time %2 sec_per_fit %3 fit_per_sec %4 fit_per_sec_per_worker_proc %5\n" )
         .arg( pm_ga_fitness_calls )
         .arg( pm_ga_fitness_secs )
         .arg( pm_ga_fitness_calls != 0 ? pm_ga_fitness_secs / (double)pm_ga_fitness_calls : 0e0 )
         .arg( pm_ga_fitness_secs != 0e0 ? (double)pm_ga_fitness_calls / pm_ga_fitness_secs : 0e0 )
#if defined( USE_MPI )
         .arg( (pm_ga_fitness_secs != 0e0 ? (double)pm_ga_fitness_calls / pm_ga_fitness_secs : 0e0 ) / (double)( npes - 1 ) )
#else 
         .arg( pm_ga_fitness_secs != 0e0 ? (double)pm_ga_fitness_calls / pm_ga_fitness_secs : 0e0 )
#endif
         ;
      ts << QString( "ga % fitness %1\n" )
         .arg( 100e0 * pm_ga_fitness_secs / ( (double) usupm_timer.times[ "pm_run" ] / 1000e0 ) );
      ts << "end-timings\n";
      QFile fc( controlfile );
      if ( fc.open( QIODevice::ReadOnly ) )
      {
         QTextStream tsc( &fc );
         ts << "controlfile:\n";
         while( !tsc.atEnd() )
         {
            ts << tsc.readLine() << endl;
         }
         ts << "end-controlfile\n";
         fc.close();
      }
      f.close();
      output_files << runinfo;
   } else {
      if ( us_log )
      {
         us_log->log( "Warning: could not create timings\n" );
      }
   }

   if ( use_tar )
   {
      // package output
      QString results_file = controlfile;
      results_file.replace( QRegExp( "\\.(tgz|TGZ|tar|TGZ)$" ), "" );
      results_file += "_out.tgz";

      if ( !create_tgz_output( results_file ) )
      {
         return false;
      }

      QDir dod( outputData );
      if ( !dod.exists() )
      {
         QDir current = QDir::current();
            
         QString newdir = outputData;
         while ( newdir.left( 3 ) == "../" )
         {
            current.cdUp();
            newdir.replace( "../", "" );
         }
         QDir::setCurrent( current.path() );
         QDir ndod;
         if ( !ndod.mkdir( newdir ) )
         {
            if ( us_log )
            {
               us_log->log( QString("Warning: could not create outputData \"%1\" directory\n" ).arg( ndod.path() ) );
            }
         }
         QDir::setCurrent( qs_base_dir );
      }
      if ( dod.exists() )
      {
         QString dest = outputData + QDir::separator() + QFileInfo( results_file ).fileName();
         QDir qd;
         if ( us_log )
         {
            us_log->log( QString("renaming: %1 to %2\n" )
                         .arg( results_file )
                         .arg( dest ) );
         }
         if ( !qd.rename( results_file, dest ) )
         {
            if ( us_log )
            {
               us_log->log( QString("Warning: could not rename outputData %1 to %2\n" )
                            .arg( results_file )
                            .arg( dest ) );
            }
         }
      } else {
         if ( us_log )
         {
            us_log->log( QString( "Error: %1 does not exist\n" ).arg( outputData ) );
         }
      }
   }

   return true;
}

bool US_Saxs_Util::run_pm( QStringList qsl_commands )
{
   output_files          .clear( );
   job_output_files      .clear( );
   write_output_count    = 0;
   timings               = "";
   bool srand48_done     = false;
   bool make_csv         = false;

   vector < QString >           csv_name;
   vector < double >            csv_q;
   vector < vector < double > > csv_I;

   QRegExp rx_blank  ( "^\\s*$" );
   QRegExp rx_comment( "#.*$" );

   QRegExp rx_valid  ( 
                      "^("
                      "pmgridsize|"
                      "pmharmonics|"
                      "pmmaxdimension|"

                      "pmrayleighdrho|"
                      "pmbufferedensity|"

                      "pmmemory|"
                      "pmdebug|"
                      "pmoutname|"

                      "pmcsv|"

                      "pmf|"
                      "pmq|"
                      "pmi|"
                      "pme|"

                      "pmgapopulation|"
                      "pmgagenerations|"
                      "pmgamutate|"
                      "pmgasamutate|"
                      "pmgacrossover|"
                      "pmgaelitism|"
                      "pmgaearlytermination|"
                      "pmgapointsmax|"

                      "pmapproxmaxdimension|"

                      "pmbestmd0stepstoga|"
                      "pmbestmd0|"

                      "pmbestga|"

                      "pmseed|"

                      // these are for grid size range
                      "pmbestfinestconversion|"
                      "pmbestcoarseconversion|"
                      "pmbestrefinementrangepct|"
                      "pmbestconversiondivisor|"

                      // these are for parameter search deltas in the current grid size
                      "pmbestdeltastart|"
                      "pmbestdeltadivisor|"
                      "pmbestdeltamin|"

                      "pmtypes|"
                      "pmparams"
                      ")$" 
                      );

   QRegExp rx_arg    ( 
                      "^("
                      "pmgridsize|"
                      "pmharmonics|"
                      "pmmaxdimension|"

                      "pmrayleighdrho|"
                      "pmbufferedensity|"

                      "pmmemory|"
                      "pmdebug|"
                      "pmoutname|"

                      "pmf|"
                      "pmq|"
                      "pmi|"
                      "pme|"

                      "pmgapopulation|"
                      "pmgagenerations|"
                      "pmgamutate|"
                      "pmgasamutate|"
                      "pmgacrossover|"
                      "pmgaelitism|"
                      "pmgaearlytermination|"
                      "pmgapointsmax|"

                      "pmapproxmaxdimension|"

                      "pmbestmd0stepstoga|"

                      "pmseed|"

                      "pmbestfinestconversion|"
                      "pmbestcoarseconversion|"
                      "pmbestrefinementrangepct|"
                      "pmbestconversiondivisor|"

                      "pmbestdeltastart|"
                      "pmbestdeltadivisor|"
                      "pmbestdeltamin|"

                      "pmtypes|"
                      "pmparams"
                      ")$" 
                      );

   QRegExp rx_vector ( 
                      "^("
                      "pmf|"
                      "pmq|"
                      "pmi|"
                      "pme|"

                      "pmtypes|"
                      "pmparams"
                      ")$" 
                      );


   vector < double > params;
   set < pm_point >  model;

   for ( int i = 0; i < (int) qsl_commands.size(); i++ )
   {
      QString qs = qsl_commands[ i ];
      qs.replace( rx_comment, "" ).replace( "^\\s+", "" ).replace( "\\s+$", "" );
      
      if ( qs.contains( rx_blank ) )
      {
         continue;
      }

      QStringList qsl = (qs ).split( QRegExp("\\s+") , QString::SkipEmptyParts );

      if ( !qsl.size() )
      {
         continue;
      }

      QString option = qsl[ 0 ].toLower();
      qsl.pop_front();

      if ( rx_valid.indexIn( option ) == -1 )
      {
         errormsg = QString( "Error controlfile line %1 : Unrecognized token %2" )
            .arg( i + 1 )
            .arg( option );
         return false;
      }

      if ( rx_arg.indexIn( option ) != -1 && 
           qsl.size() < 1 )
      {
         errormsg = QString( "Error reading controlfile line %1 : Missing argument " )
            .arg( i + 1 );
         return false;
      }

      if ( rx_arg.indexIn( option ) != -1 )
      {
         control_parameters[ option ] = qsl.join( " " );
      }

      if ( rx_vector.indexIn( option ) != -1 )
      {
         control_vectors[ option ].clear( );
         if ( us_log )
         {
            us_log->log( QString( "qsl currently: %1\n" ).arg( qsl.join( "~" ) ) );
         }
         for ( int j = 0; j < (int) qsl.size(); j++ )
         {
            QStringList qsl2;
            {
               QRegExp rx = QRegExp( "(\\s+|(\\s*(,|:)\\s*))" );
               qsl2 = (qsl[ j ] ).split( rx , QString::SkipEmptyParts );
            }
            if ( us_log )
            {
               us_log->log( QString( "qsl2 currently: %1\n" ).arg( qsl2.join( "~" ) ) );
            }
            for ( int k = 0; k < (int) qsl2.size(); k++ )
            {
               control_vectors[ option ].push_back( qsl2[ k ].toDouble() );
            }
         }
         if ( us_log )
         {
            us_log->log( US_Vector::qs_vector( option, control_vectors[ option ] ) );
         }
      }


      if ( option == "pmq" && make_csv && csv_name.size() )
      {
         if ( !flush_pm_csv( csv_name,
                             csv_q,
                             csv_I ) )
         {
            return false;
         }
      }

      if ( option == "pmcsv" )
      {
         make_csv = true;
         continue;
      }

      if ( option == "pmrayleighdrho" )
      {
         if ( !control_vectors.count( "pmq" ) )
         {
            errormsg = "pmq must be defined before pmrayleighdro";
            return false;
         }
         if ( !control_parameters.count( "pmgridsize" ) )
         {
            errormsg = "pmgridsize must be defined before pmrayleighdro";
            return false;
         }
         if ( !control_parameters.count( "pmbufferedensity" ) )
         {
            errormsg = "pmbufferedensity must be defined before pmrayleighdro";
            return false;
         }

         if ( !compute_rayleigh_structure_factors( 
                                                  pow( pow( control_parameters[ "pmgridsize" ].toDouble(), 3e0 ) / M_PI, 1e0/3e0 ),
                                                  control_parameters[ "pmrayleighdrho" ].toDouble(),
                                                  control_vectors   [ "pmq" ],
                                                  control_vectors   [ "pmf" ] ) )
         {
            errormsg = QString( "Error controlfile line %1 : %2" ).arg( i + 1 ).arg( errormsg );
            return false;
         }
         double bed = control_parameters[ "pmbufferedensity" ].toDouble();
         if ( bed )
         {
            double vi = pow( control_parameters[ "pmgridsize" ].toDouble(), 3e0 );
            double vie = vi * bed;
            double vi_23_4pi = - pow((double)vi,2.0/3.0) * M_ONE_OVER_4PI;
            for ( int i = 0; i < (int) control_vectors[ "pmf" ].size(); ++i )
            {
               double q = control_vectors[ "pmq" ][ i ];
               control_vectors[ "pmf" ][ i ] -= vie * exp( vi_23_4pi * q * q );
            }
         }

         if ( us_log )
         {
            us_log->log( "Rayleigh structure factors computed\n" );
         }
         
         // US_Vector::printvector2( "pmq pmf", control_vectors[ "pmq" ], control_vectors[ "pmf" ] );
         continue;
      }                                                  

      if ( option == "pmapproxmaxdimension" )
      {
         control_parameters.erase( "approx_max_d" );

         if ( !run_pm_ok( option ) )
         {
            errormsg = QString( "Error controlfile line %1 : %2" ).arg( i + 1 ).arg( errormsg );
            return false;
         }            

         US_PM pm(
                  control_parameters [ "pmgridsize"     ].toDouble(),
                  control_parameters [ "pmmaxdimension" ].toInt(),
                  control_parameters [ "pmharmonics"    ].toUInt(),
                  control_vectors    [ "pmf"            ],
                  control_vectors    [ "pmq"            ],
                  control_vectors    [ "pmi"            ],
                  control_vectors    [ "pme"            ],
                  control_parameters [ "pmmemory"       ].toUInt(),
                  control_parameters [ "pmdebug"        ].toInt()
                  );

         unsigned int approx_max_d;
         if ( !pm.approx_max_dimension( params, 
                                        control_parameters[ "pmbestcoarseconversion" ].toDouble(),
                                        approx_max_d ) )
         {
            return false;
         }
         control_parameters[ "approx_max_d" ] = QString( "%1" ).arg( approx_max_d );
         if ( us_log )
         {
            us_log->log( QString( "best maxd %1\n" ).arg( control_parameters[ "approx_max_d" ] ) );
         }
         continue;
      }

      if ( option == "pmbestmd0" )
      {
         if ( !run_pm_ok( option ) )
         {
            errormsg = QString( "Error controlfile line %1 : %2" ).arg( i + 1 ).arg( errormsg );
            return false;
         }            

         if ( control_vectors.count( "pmtypes" ) &&
              control_vectors[ "pmtypes" ].size() != 1 )
         {
            errormsg = QString( "Error controlfile line %1 : pmtypes must have exactly one parameter for pmbestmd0" ).arg( i + 1 );
            return false;
         }

         if ( control_parameters.count( "pmseed" ) &&
              control_parameters[ "pmseed" ].toLong() != 0L )
         {
            srand48( control_parameters[ "pmseed" ].toLong() );
            srand48_done = true;
         } else {
            if ( !srand48_done )
            {
               long int li = ( long int )QTime::currentTime().msec();
               if ( us_log )
               {
                  us_log->log( QString( "to reproduce use random seed %1\n" ).arg( li ) );
               }
               srand48( li );
            }
            srand48_done = true;
         }

         US_PM pm(
                  control_parameters [ "pmgridsize"     ].toDouble(),
                  control_parameters [ "pmmaxdimension" ].toInt(),
                  control_parameters [ "pmharmonics"    ].toUInt(),
                  control_vectors    [ "pmf"            ],
                  control_vectors    [ "pmq"            ],
                  control_vectors    [ "pmi"            ],
                  control_vectors    [ "pme"            ],
                  control_parameters [ "pmmemory"       ].toUInt(),
                  control_parameters [ "pmdebug"        ].toInt()
                  );

#if defined( USE_MPI )
         {

            pm.pm_workers_registered.clear( );
            pm.pm_workers_busy      .clear( );
            pm.pm_workers_waiting   .clear( );
   
            for ( int i = 1; i < npes; ++i )
            {
               pm.pm_workers_registered.insert( i );
               pm.pm_workers_waiting   .insert( i );
            }

            pm_msg msg;
            int errorno                = -28000;
            msg.type                   = PM_NEW_PM;
            msg.flags                  = pm.use_errors ? PM_USE_ERRORS : 0;
            msg.vsize                  = (uint32_t) control_vectors[ "pmq" ].size();
            msg.grid_conversion_factor = control_parameters[ "pmgridsize" ].toDouble();
            msg.max_dimension          = (uint32_t) control_parameters[ "pmmaxdimension" ].toUInt();
            msg.max_harmonics          = (uint32_t) control_parameters[ "pmharmonics" ].toUInt();
            msg.max_mem_in_MB          = (uint32_t) control_parameters[ "pmmemory" ].toUInt();

            unsigned int tot_vsize = msg.vsize * ( pm.use_errors ? 4 : 3 );
            vector < double > d( tot_vsize );

            if ( pm.use_errors )
            {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
                  d[ 3 * msg.vsize + i ] = control_vectors[ "pme" ][ i ];
               }
            } else {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
               }
            }

            for ( int i = 1; i < npes; ++i )
            {
               if ( MPI_SUCCESS != MPI_Send( &msg,
                                             sizeof( pm_msg ),
                                             MPI_CHAR, 
                                             i,
                                             PM_MSG, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in best_md0_ga() PM_NEW_PM\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }

               if ( MPI_SUCCESS != MPI_Send( &(d[0]),
                                             tot_vsize * sizeof( double ),
                                             MPI_CHAR, 
                                             i,
                                             PM_NEW_PM, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in best_md0_ga() PM_NEW_PM_DATA\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }
            }
         }
#endif
         pm.ga_set_params( 
                          control_parameters[ "pmgapopulation"       ].toUInt(),
                          control_parameters[ "pmgagenerations"      ].toUInt(),
                          control_parameters[ "pmgamutate"           ].toDouble(),
                          control_parameters[ "pmgasamutate"         ].toDouble(),
                          control_parameters[ "pmgacrossover"        ].toDouble(),
                          control_parameters[ "pmgaelitism"          ].toUInt(),
                          control_parameters[ "pmgaearlytermination" ].toUInt()
                          );

         pm.set_best_delta(
                           control_parameters[ "pmbestdeltastart"   ].toDouble(),
                           control_parameters[ "pmbestdeltadivisor" ].toDouble(),
                           control_parameters[ "pmbestdeltamin"     ].toDouble()
                           );

         if ( control_vectors.count( "pmparams" ) )
         {
            params = control_vectors[ "pmparams" ];
         } else {
            if ( control_vectors.count( "pmtypes" ) )
            {
               vector < int > types;
               for ( int i = 0; i < (int) control_vectors[ "pmtypes" ].size(); i++ )
               {
                  types.push_back( (int) control_vectors[ "pmtypes" ][ i ] );
               }
               pm.zero_params( params, types );
            } else {
               errormsg = QString( "Error controlfile line %1 : pmparams or pmtypes must be defined" ).arg( i + 1 );
               return false;
            }
         }
         if ( !pm.best_md0_ga( 
                              params,
                              model,
                              control_parameters[ "pmbestmd0stepstoga"       ].toUInt(),
                              control_parameters[ "pmgapointsmax"            ].toUInt(),
                              control_parameters[ "pmbestfinestconversion"   ].toDouble(),
                              control_parameters[ "pmbestcoarseconversion"   ].toDouble(),
                              control_parameters[ "pmbestrefinementrangepct" ].toDouble(),
                              control_parameters[ "pmbestconversiondivisor"  ].toDouble()
                              ) )
         {
            errormsg = "Error:" + pm.error_msg;
            return false;
         }

         pm_ga_fitness_secs  += pm.pm_ga_fitness_secs;
         pm_ga_fitness_calls += pm.pm_ga_fitness_calls;

         QString outname = control_parameters[ "pmoutname" ];
         if ( !pm.write_model( outname, model, params, false ) )
         {
            errormsg = QString( "Error writing model %1" ).arg( outname );
            return false;
         }
         output_files << QString( outname + ".bead_model" );
         if ( !pm.write_I    ( outname, model, false ) )
         {
            errormsg = QString( "Error writing I data %1" ).arg( outname );
            return false;
         }
         output_files << QString( outname + ".dat" );
         if ( make_csv )
         {
            csv_q   = control_vectors[ "pmq" ];
            csv_name.push_back( outname );
            csv_I   .push_back( pm.last_written_I );
         }
         continue;
      }         

      if ( option == "pmbestga" )
      {
         if ( !run_pm_ok( option ) )
         {
            errormsg = QString( "Error controlfile line %1 : %2" ).arg( i + 1 ).arg( errormsg );
            return false;
         }            

         if ( !control_parameters.count( "approx_max_d" ) &&
              control_parameters[ "approx_max_d" ].toUInt() == 0 )
         {
            errormsg = QString( "Error controlfile line %1 : pmappromxmaxdimension option must be selected prior to pmbestga" ).arg( i + 1 );
            return false;
         }

         if ( control_parameters.count( "pmseed" ) &&
              control_parameters[ "pmseed" ].toLong() != 0L )
         {
            srand48( control_parameters[ "pmseed" ].toLong() );
            srand48_done = true;
         } else {
            if ( !srand48_done )
            {
               long int li = ( long int )QTime::currentTime().msec();
               if ( us_log )
               {
                  us_log->log( QString( "to reproduce use random seed %1\n" ).arg( li ) );
               }
               srand48( li );
            }
            srand48_done = true;
         }

         US_PM pm(
                  control_parameters [ "pmgridsize"     ].toDouble(),
                  control_parameters [ "approx_max_d"   ].toUInt(),
                  control_parameters [ "pmharmonics"    ].toUInt(),
                  control_vectors    [ "pmf"            ],
                  control_vectors    [ "pmq"            ],
                  control_vectors    [ "pmi"            ],
                  control_vectors    [ "pme"            ],
                  control_parameters [ "pmmemory"       ].toUInt(),
                  control_parameters [ "pmdebug"        ].toInt()
                  );

#if defined( USE_MPI )
         {

            pm.pm_workers_registered.clear( );
            pm.pm_workers_busy      .clear( );
            pm.pm_workers_waiting   .clear( );
   
            for ( int i = 1; i < npes; ++i )
            {
               pm.pm_workers_registered.insert( i );
               pm.pm_workers_waiting   .insert( i );
            }

            pm_msg msg;
            int errorno                = -28000;
            msg.type                   = PM_NEW_PM;
            msg.flags                  = pm.use_errors ? PM_USE_ERRORS : 0;
            msg.vsize                  = (uint32_t) control_vectors[ "pmq" ].size();
            msg.grid_conversion_factor = control_parameters[ "pmgridsize" ].toDouble();
            msg.max_dimension          = (uint32_t) control_parameters[ "pmmaxdimension" ].toUInt();
            msg.max_harmonics          = (uint32_t) control_parameters[ "pmharmonics" ].toUInt();
            msg.max_mem_in_MB          = (uint32_t) control_parameters[ "pmmemory" ].toUInt();

            unsigned int tot_vsize = msg.vsize * ( pm.use_errors ? 4 : 3 );
            vector < double > d( tot_vsize );

            if ( pm.use_errors )
            {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
                  d[ 3 * msg.vsize + i ] = control_vectors[ "pme" ][ i ];
               }
            } else {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
               }
            }

            for ( int i = 1; i < npes; ++i )
            {
               if ( MPI_SUCCESS != MPI_Send( &msg,
                                             sizeof( pm_msg ),
                                             MPI_CHAR, 
                                             i,
                                             PM_MSG, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in bestga() PM_NEW_PM\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }

               if ( MPI_SUCCESS != MPI_Send( &(d[0]),
                                             tot_vsize * sizeof( double ),
                                             MPI_CHAR, 
                                             i,
                                             PM_NEW_PM, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in bestga() PM_NEW_PM_DATA\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }
            }
         }
#endif
         pm.ga_set_params( 
                          control_parameters[ "pmgapopulation"       ].toUInt(),
                          control_parameters[ "pmgagenerations"      ].toUInt(),
                          control_parameters[ "pmgamutate"           ].toDouble(),
                          control_parameters[ "pmgasamutate"         ].toDouble(),
                          control_parameters[ "pmgacrossover"        ].toDouble(),
                          control_parameters[ "pmgaelitism"          ].toUInt(),
                          control_parameters[ "pmgaearlytermination" ].toUInt()
                          );

         pm.set_best_delta(
                           control_parameters[ "pmbestdeltastart"   ].toDouble(),
                           control_parameters[ "pmbestdeltadivisor" ].toDouble(),
                           control_parameters[ "pmbestdeltamin"     ].toDouble()
                           );

         if ( control_vectors.count( "pmparams" ) )
         {
            params = control_vectors[ "pmparams" ];
         } else {
            if ( control_vectors.count( "pmtypes" ) )
            {
               vector < int > types;
               for ( int i = 0; i < (int) control_vectors[ "pmtypes" ].size(); i++ )
               {
                  types.push_back( (int) control_vectors[ "pmtypes" ][ i ] );
               }
               pm.zero_params( params, types );
            } else {
               errormsg = QString( "Error controlfile line %1 : pmparams or pmtypes must be defined" ).arg( i + 1 );
               return false;
            }
         }
         if ( !pm.best_ga( 
                          params,
                          model,
                          control_parameters[ "pmgapointsmax"            ].toUInt(),
                          control_parameters[ "pmbestfinestconversion"   ].toDouble(),
                          control_parameters[ "pmbestcoarseconversion"   ].toDouble(),
                          control_parameters[ "pmbestrefinementrangepct" ].toDouble(),
                          control_parameters[ "pmbestconversiondivisor"  ].toDouble()
                          ) )
         {
            errormsg = "Error:" + pm.error_msg;
            return false;
         }

         pm_ga_fitness_secs  += pm.pm_ga_fitness_secs;
         pm_ga_fitness_calls += pm.pm_ga_fitness_calls;

         QString outname = control_parameters[ "pmoutname" ];
         if ( !pm.write_model( outname, model, params, false ) )
         {
            errormsg = QString( "Error writing model %1" ).arg( outname );
            return false;
         }
         output_files << QString( outname + ".bead_model" );
         if ( !pm.write_I    ( outname, model, false ) )
         {
            errormsg = QString( "Error writing I data %1" ).arg( outname );
            return false;
         }
         output_files << QString( outname + ".dat" );
         if ( make_csv )
         {
            csv_q   = control_vectors[ "pmq" ];
            csv_name.push_back( outname );
            csv_I   .push_back( pm.last_written_I );
         }
         continue;
      }         
   }

   if ( make_csv && csv_name.size() )
   {
      if ( !flush_pm_csv( csv_name,
                          csv_q,
                          csv_I ) )
      {
         return false;
      }
   }

   return true;
}

bool US_Saxs_Util::run_pm_ok( QString option ) 
{
   // required params
   {
      QStringList params_required;
      params_required
         << "pmgridsize"
         ;
      
      for ( int i = 0; i < (int) params_required.size(); i++ )
      {
         if ( !control_parameters.count( params_required[ i ] ) )
         {
            errormsg = QString( "missing required parameter %1" ).arg( params_required[ i ] );
            return false;
         }
      }
   }

   // defaultable params
   {
      QStringList default_params;
      default_params 
         << "pmharmonics"
         << "pmmindelta"

         << "pmmaxdimension"

         << "pmmemory"
         << "pmdebug"
         << "pmoutname"

         << "pmgapopulation"
         << "pmgagenerations"
         << "pmgamutate"
         << "pmgasamutate"
         << "pmgacrossover"
         << "pmgaelitism"
         << "pmgaearlytermination"
         << "pmgapointsmax"

         << "pmbestfinestconversion"
         << "pmbestcoarseconversion"
         << "pmbestrefinementrangepct"
         << "pmbestconversiondivisor"

         << "pmbestdeltastart"
         << "pmbestdeltadivisor"
         << "pmbestdeltamin"

         ;

      if ( option == "pmbestmd0" )
      {
         default_params
            << "pmbestmd0stepstoga"
            ;
      }

      map < QString, QString > defaults;
      defaults[ "pmharmonics"              ] = "15";
      defaults[ "pmmindelta"               ] = "0.01";

      defaults[ "pmmaxdimension"           ] = QString( "%1" ).arg( USPM_MAX_VAL );

      defaults[ "pmdebug"                  ] = "0";
      defaults[ "pmmemory"                 ] = "1024";
      defaults[ "pmoutname"                ] = "pm_output";

      defaults[ "pmgapopulation"           ] = "100";
      defaults[ "pmgagenerations"          ] = "100";
      defaults[ "pmgamutate"               ] = "0.1e0";
      defaults[ "pmgasamutate"             ] = "0.5e0";
      defaults[ "pmgacrossover"            ] = "0.4e0";
      defaults[ "pmgaelitism"              ] = "1";
      defaults[ "pmgaearlytermination"     ] = "5";
      defaults[ "pmgapointsmax"            ] = "100";

      defaults[ "pmbestmd0stepstoga"       ] = "1";

      defaults[ "pmbestfinestconversion"   ] = control_parameters[ "pmgridsize" ];
      defaults[ "pmbestcoarseconversion"   ] = "10e0";
      defaults[ "pmbestrefinementrangepct" ] = "2.5e0";
      defaults[ "pmbestconversiondivisor"  ] = "2.5e0";

      defaults[ "pmbestdeltastart"         ] = "1e0";
      defaults[ "pmbestdeltadivisor"       ] = "10e0";
      defaults[ "pmbestdeltamin"           ] = "1e-2";

      if ( us_log )
      {
         us_log->flushoff();
      }
      for ( int i = 0; i < (int) default_params.size(); i++ )
      {
         if ( !control_parameters.count( default_params[ i ] ) )
         {
            control_parameters[ default_params[ i ] ] = defaults[ default_params[ i ] ];
            if ( us_log )
            {
               us_log->log( QString( "Notice: setting '%1' to default value of %2\n" )
                             .arg( default_params[ i ] )
                             .arg( defaults[ default_params[ i ] ] ) )
                  ;
            }
         }
      }
      if ( us_log )
      {
         us_log->flushon();
      }
   }

   // the fixed common length vectors
   {
      QStringList qslv_required;
      qslv_required 
         << "pmi"
         << "pmq"
         << "pmf"
         ;
      
      QStringList qslv_optional;
      qslv_optional
         << "pme"
         ;
   
      int vector_size = 0;

      for ( int i = 0; i < (int) qslv_required.size(); i++ )
      {
         if ( !control_vectors.count( qslv_required[ i ] ) )
         {
            errormsg = QString( "missing required vector %1" ).arg( qslv_required[ i ] );
            return false;
         }
         if ( !i )
         {
            vector_size = (int) control_vectors[ qslv_required[ i ] ].size();
         } else {
            if ( vector_size != (int)control_vectors[ qslv_required[ i ] ].size() )
            {
               errormsg = QString( "required vector %1 incompatible size %2 vs %3" )
                  .arg( qslv_required[ i ] )
                  .arg( (int)control_vectors[ qslv_required[ i ] ].size() )
                  .arg( vector_size )
                  ;
               return false;
            }
         }
      }

      if ( vector_size < 3 )
      {
         errormsg = QString( "vector %1 size %2 less than minimum size 3" )
            .arg( qslv_required[ 0 ] )
            .arg( vector_size )
            ;
         return false;
      }

      for ( int i = 0; i < (int) qslv_optional.size(); i++ )
      {
         if ( control_vectors.count( qslv_optional[ i ] ) &&
              control_vectors[ qslv_optional[ i ] ].size() &&
              vector_size != (int)control_vectors[ qslv_optional[ i ] ].size() )
         {
            errormsg = QString( "optional vector %1 incompatible size %2 vs %3" )
               .arg( qslv_optional[ i ] )
               .arg( (int)control_vectors[ qslv_optional[ i ] ].size() )
               .arg( vector_size )
               ;
            return false;
         }
      }
   }

   // the parameters/types
   if ( option != "pmapproxmaxdimension" )
   {
      // only one of these required
      QStringList qslv_required;
      qslv_required 
         << "pmparams"
         << "pmtypes"
         ;

      int number_of_params_found = 0;
      for ( int i = 0; i < (int) qslv_required.size(); i++ )
      {
         if ( control_vectors.count( qslv_required[ i ] ) )
         {
            number_of_params_found++;
         }
      }
      if ( !number_of_params_found && !control_parameters.count( "pmtypes" ) )
      {
         errormsg = QString( "missing required vector one of: %1" ).arg( qslv_required.join( "," ) );
         return false;
      }
      if ( number_of_params_found > 1 )
      {
         if ( us_log )
         {
            us_log->log( "Warning: pmparams superceded pmtypes (both were defined)\n" );
         }
      }
   }

   if ( us_log )
   {
      for ( map < QString, QString >::iterator it = control_parameters.begin();
            it != control_parameters.end();
            it++ )
      {
         us_log->log( QString( "\"%1\"\t%2\t%3\n" ).arg( it->first ).arg( it->second ).arg( it->second.toDouble() ) );
      }
   }

   return true;
}
         
// callable version

bool US_Saxs_Util::run_pm( 
                          map < QString, vector < double > > & produced_q,
                          map < QString, vector < double > > & produced_I,
                          map < QString, QString >           & produced_model,
                          map < QString, QString >           & parameters,
                          map < QString, vector < double > > & vectors,
                          bool quiet
                           )
{
   output_files          .clear( );
   job_output_files      .clear( );
   write_output_count    = 0;
   timings               = "";
   bool srand48_done     = false;

   control_parameters = parameters;
   control_vectors    = vectors;

   vector < double > params;
   set < pm_point >  model;

   errormsg = "";

   // bool use_errors = 
   //    control_vectors[ "pme" ].size() == control_vectors[ "pmq" ].size() &&
   //    !is_nonzero_vector( control_vectors[ "pme" ] );

   if ( control_parameters.count( "pmrayleighdrho" ) )
   {
      if ( !control_vectors.count( "pmq" ) )
      {
         errormsg = "pmq must be defined";
         return false;
      }
      if ( !control_parameters.count( "pmgridsize" ) )
      {
         errormsg = "pmgridsize must be defined";
         return false;
      }
      if ( !control_parameters.count( "pmbufferedensity" ) )
      {
         errormsg = "pmbufferedensity must be defined";
         return false;
      }
      
      if ( !compute_rayleigh_structure_factors( 
                                               pow( pow( control_parameters[ "pmgridsize" ].toDouble(), 3e0 ) / M_PI, 1e0/3e0 ),
                                               control_parameters[ "pmrayleighdrho" ].toDouble(),
                                               control_vectors   [ "pmq" ],
                                               control_vectors   [ "pmf" ] ) )
      {
         errormsg = QString( "Error computing structure factors : %1" ).arg( errormsg );
         return false;
      }
      double bed = control_parameters[ "pmbufferedensity" ].toDouble();
      if ( bed )
      {
         double vi = pow( control_parameters[ "pmgridsize" ].toDouble(), 3e0 );
         double vie = vi * bed;
         double vi_23_4pi = - pow((double)vi,2.0/3.0) * M_ONE_OVER_4PI;
         for ( int i = 0; i < (int) control_vectors[ "pmf" ].size(); ++i )
         {
            double q = control_vectors[ "pmq" ][ i ];
            control_vectors[ "pmf" ][ i ] -= vie * exp( vi_23_4pi * q * q );
         }
      }
      
      if ( !quiet && us_log )
      {
         us_log->log( "Rayleigh structure factors computed\n" );
         us_log->log( US_Vector::qs_vector2( "pmq pmf", control_vectors[ "pmq" ], control_vectors[ "pmf" ] ) );
      }

      if ( control_parameters.count( "pmapproxmaxdimension" ) )
      {
         control_parameters.erase( "approx_max_d" );

         if ( !run_pm_ok( "pmapproxmaxdimension" ) )
         {
            errormsg = QString( "Error when computing approx max dimension : %1" ).arg( errormsg );
            return false;
         }            

         if ( !quiet && us_log )
         {
            us_log->log( QString( "uspm approxmaxdim %1" ).arg( control_parameters [ "pmgridsize"     ].toDouble() ) );
         }

         if ( us_udp_msg )
         {
            map < QString, QString > msging;
            msging[ "status" ] = 
               QString( "computing approximate maximum dimension of %1" )
               .arg( control_parameters.count( "pmoutname" ) ? control_parameters[ "pmoutname" ] : "unknown" );
            us_udp_msg->send_json( msging );
         }
         US_PM pm(
                  control_parameters [ "pmgridsize"     ].toDouble(),
                  control_parameters [ "pmmaxdimension" ].toInt(),
                  control_parameters [ "pmharmonics"    ].toUInt(),
                  control_vectors    [ "pmf"            ],
                  control_vectors    [ "pmq"            ],
                  control_vectors    [ "pmi"            ],
                  control_vectors    [ "pme"            ],
                  control_parameters [ "pmmemory"       ].toUInt(),
                  control_parameters [ "pmdebug"        ].toInt()
                  );

         unsigned int approx_max_d;

         if ( !pm.approx_max_dimension( params, 
                                        control_parameters[ "pmbestcoarseconversion" ].toDouble(),
                                        approx_max_d ) )
         {
            return false;
         }
         if ( us_udp_msg )
         {
            map < QString, QString > msging;
            msging[ "status" ] = 
               QString( "approximate maximum dimension of %1 is %2" )
               .arg( control_parameters.count( "pmoutname" ) ? control_parameters[ "pmoutname" ] : "unknown" )
               .arg( approx_max_d );
            us_udp_msg->send_json( msging );
         }
         control_parameters[ "approx_max_d" ] = QString( "%1" ).arg( approx_max_d );
         if ( !quiet && us_log )
         {
            us_log->log( QString( "approx maxd %1\n" ).arg( control_parameters[ "approx_max_d" ] ) );
         }
      }

      if ( control_parameters.count( "pmbestmd0" ) )
      {
         if ( !quiet && us_log )
         {
            us_log->log( "pmbestmd0\n" );
         }

         if ( !run_pm_ok( "pmbestmd0" ) )
         {
            errormsg = QString( "Error pmbestmd0: %1" ).arg( errormsg );
            return false;
         }            

         if ( control_vectors.count( "pmtypes" ) &&
              control_vectors[ "pmtypes" ].size() != 1 )
         {
            errormsg = QString( "Error pmbestmd0 : pmtypes must have exactly one parameter for pmbestmd0" );
            return false;
         }

         if ( control_parameters.count( "pmseed" ) &&
              control_parameters[ "pmseed" ].toLong() != 0L )
         {
            srand48( control_parameters[ "pmseed" ].toLong() );
            srand48_done = true;
         } else {
            if ( !srand48_done )
            {
               long int li = ( long int )QTime::currentTime().msec();
               if ( !quiet && us_log )
               {
                  us_log->log( QString( "to reproduce use random seed %1\n" ).arg( li ) );
               }
               srand48( li );
            }
            srand48_done = true;
         }

         us_qdebug( QString( "uspm bestmd0 %1" ).arg( control_parameters [ "pmgridsize"     ].toDouble() ) );

         US_PM pm(
                  control_parameters [ "pmgridsize"     ].toDouble(),
                  control_parameters [ "pmmaxdimension" ].toInt(),
                  control_parameters [ "pmharmonics"    ].toUInt(),
                  control_vectors    [ "pmf"            ],
                  control_vectors    [ "pmq"            ],
                  control_vectors    [ "pmi"            ],
                  control_vectors    [ "pme"            ],
                  control_parameters [ "pmmemory"       ].toUInt(),
                  control_parameters [ "pmdebug"        ].toInt()
                  );

#if defined( USE_MPI )
         {

            pm.pm_workers_registered.clear( );
            pm.pm_workers_busy      .clear( );
            pm.pm_workers_waiting   .clear( );
   
            for ( int i = 1; i < npes; ++i )
            {
               pm.pm_workers_registered.insert( i );
               pm.pm_workers_waiting   .insert( i );
            }

            pm_msg msg;
            int errorno                = -28000;
            msg.type                   = PM_NEW_PM;
            msg.flags                  = pm.use_errors ? PM_USE_ERRORS : 0;
            msg.vsize                  = (uint32_t) control_vectors[ "pmq" ].size();
            msg.grid_conversion_factor = control_parameters[ "pmgridsize" ].toDouble();
            msg.max_dimension          = (uint32_t) control_parameters[ "pmmaxdimension" ].toUInt();
            msg.max_harmonics          = (uint32_t) control_parameters[ "pmharmonics" ].toUInt();
            msg.max_mem_in_MB          = (uint32_t) control_parameters[ "pmmemory" ].toUInt();

            unsigned int tot_vsize = msg.vsize * ( pm.use_errors ? 4 : 3 );
            vector < double > d( tot_vsize );

            if ( pm.use_errors )
            {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
                  d[ 3 * msg.vsize + i ] = control_vectors[ "pme" ][ i ];
               }
            } else {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
               }
            }

            for ( int i = 1; i < npes; ++i )
            {
               if ( MPI_SUCCESS != MPI_Send( &msg,
                                             sizeof( pm_msg ),
                                             MPI_CHAR, 
                                             i,
                                             PM_MSG, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in best_md0_ga() PM_NEW_PM\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }

               if ( MPI_SUCCESS != MPI_Send( &(d[0]),
                                             tot_vsize * sizeof( double ),
                                             MPI_CHAR, 
                                             i,
                                             PM_NEW_PM, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in best_md0_ga() PM_NEW_PM_DATA\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }
            }
         }
#endif

         pm.ga_set_params( 
                          control_parameters[ "pmgapopulation"       ].toUInt(),
                          control_parameters[ "pmgagenerations"      ].toUInt(),
                          control_parameters[ "pmgamutate"           ].toDouble(),
                          control_parameters[ "pmgasamutate"         ].toDouble(),
                          control_parameters[ "pmgacrossover"        ].toDouble(),
                          control_parameters[ "pmgaelitism"          ].toUInt(),
                          control_parameters[ "pmgaearlytermination" ].toUInt()
                          );

         pm.set_best_delta(
                           control_parameters[ "pmbestdeltastart"   ].toDouble(),
                           control_parameters[ "pmbestdeltadivisor" ].toDouble(),
                           control_parameters[ "pmbestdeltamin"     ].toDouble()
                           );

         vector < int > types;
         if ( control_vectors.count( "pmparams" ) )
         {
            params = control_vectors[ "pmparams" ];
         } else {
            if ( control_vectors.count( "pmtypes" ) )
            {
               for ( int i = 0; i < (int) control_vectors[ "pmtypes" ].size(); i++ )
               {
                  types.push_back( (int) control_vectors[ "pmtypes" ][ i ] );
               }
               pm.zero_params( params, types );
            } else {
               errormsg = QString( "Error bestga : pmparams or pmtypes must be defined" );
               return false;
            }
         }
         if ( !pm.best_md0_ga( 
                              params,
                              model,
                              control_parameters[ "pmbestmd0stepstoga"       ].toUInt(),
                              control_parameters[ "pmgapointsmax"            ].toUInt(),
                              control_parameters[ "pmbestfinestconversion"   ].toDouble(),
                              control_parameters[ "pmbestcoarseconversion"   ].toDouble(),
                              control_parameters[ "pmbestrefinementrangepct" ].toDouble(),
                              control_parameters[ "pmbestconversiondivisor"  ].toDouble()
                              ) )
         {
            errormsg = "Error:" + pm.error_msg;
            return false;
         }

         pm_ga_fitness_secs  += pm.pm_ga_fitness_secs;
         pm_ga_fitness_calls += pm.pm_ga_fitness_calls;

         QString outname = control_parameters[ "pmoutname" ] + pm.get_name( types );

         int ext = 0;
         while ( produced_I.count( outname ) )
         {
            outname = control_parameters[ "pmoutname" ] + pm.get_name( types ) + QString( "-%1" ).arg( ++ext );
         }
         
         produced_q[ outname ] = pm.q;
         produced_I[ outname ].resize( pm.q.size() );
         
         if ( !pm.qstring_model( produced_model[ outname ], model, params ) )
         {
            errormsg = QString( "Error producing model %1" ).arg( outname );
            return false;
         }
            
         if ( !pm.compute_I( model, produced_I[ outname ] ) )
         {
            errormsg = pm.error_msg;
            return false;
         }

         // if ( !pm.write_model( outname, model, params, false ) )
         // {
         //    errormsg = QString( "Error writing model %1" ).arg( outname );
         //    return false;
         // }
         // output_files << QString( outname + ".bead_model" );
         // if ( !pm.write_I    ( outname, model, false ) )
         // {
         //    errormsg = QString( "Error writing I data %1" ).arg( outname );
         //    return false;
         // }
         // output_files << QString( outname + ".dat" );
      }         

      if ( control_parameters.count( "pmbestga" ) )
      {
         if ( !run_pm_ok( "pmbestga" ) )
         {
            errormsg = QString( "Error pmbestga : %1" ).arg( errormsg );
            return false;
         }            

         if ( !control_parameters.count( "approx_max_d" ) &&
              control_parameters[ "approx_max_d" ].toUInt() == 0 )
         {
            errormsg = QString( "Error pmbestga : pmappromxmaxdimension option must be selected prior to pmbestga" );
            return false;
         }

         if ( control_parameters.count( "pmseed" ) &&
              control_parameters[ "pmseed" ].toLong() != 0L )
         {
            srand48( control_parameters[ "pmseed" ].toLong() );
            srand48_done = true;
         } else {
            if ( !srand48_done )
            {
               long int li = ( long int )QTime::currentTime().msec();
               if ( us_log )
               {
                  us_log->log( QString( "to reproduce use random seed %1\n" ).arg( li ) );
               }
               srand48( li );
            }
            srand48_done = true;
         }

         if ( !quiet && us_log )
         {
            us_log->log( QString( "uspm bestga again %1" ).arg( control_parameters [ "pmgridsize"     ].toDouble() ) );
         }

         US_PM pm(
                  control_parameters [ "pmgridsize"     ].toDouble(),
                  control_parameters [ "approx_max_d"   ].toUInt(),
                  control_parameters [ "pmharmonics"    ].toUInt(),
                  control_vectors    [ "pmf"            ],
                  control_vectors    [ "pmq"            ],
                  control_vectors    [ "pmi"            ],
                  control_vectors    [ "pme"            ],
                  control_parameters [ "pmmemory"       ].toUInt(),
                  control_parameters [ "pmdebug"        ].toInt()
                  );

#if defined( USE_MPI )
         {

            pm.pm_workers_registered.clear( );
            pm.pm_workers_busy      .clear( );
            pm.pm_workers_waiting   .clear( );
   
            for ( int i = 1; i < npes; ++i )
            {
               pm.pm_workers_registered.insert( i );
               pm.pm_workers_waiting   .insert( i );
            }

            pm_msg msg;
            int errorno                = -28000;
            msg.type                   = PM_NEW_PM;
            msg.flags                  = pm.use_errors ? PM_USE_ERRORS : 0;
            msg.vsize                  = (uint32_t) control_vectors[ "pmq" ].size();
            msg.grid_conversion_factor = control_parameters[ "pmgridsize" ].toDouble();
            msg.max_dimension          = (uint32_t) control_parameters[ "pmmaxdimension" ].toUInt();
            msg.max_harmonics          = (uint32_t) control_parameters[ "pmharmonics" ].toUInt();
            msg.max_mem_in_MB          = (uint32_t) control_parameters[ "pmmemory" ].toUInt();

            unsigned int tot_vsize = msg.vsize * ( pm.use_errors ? 4 : 3 );
            vector < double > d( tot_vsize );

            if ( pm.use_errors )
            {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
                  d[ 3 * msg.vsize + i ] = control_vectors[ "pme" ][ i ];
               }
            } else {
               for ( int i = 0; i < msg.vsize; i++ )
               {
                  d[ i ]                 = control_vectors[ "pmf" ][ i ];
                  d[ msg.vsize + i ]     = control_vectors[ "pmq" ][ i ];
                  d[ 2 * msg.vsize + i ] = control_vectors[ "pmi" ][ i ];
               }
            }

            for ( int i = 1; i < npes; ++i )
            {
               if ( MPI_SUCCESS != MPI_Send( &msg,
                                             sizeof( pm_msg ),
                                             MPI_CHAR, 
                                             i,
                                             PM_MSG, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in best_md0_ga() PM_NEW_PM\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }

               if ( MPI_SUCCESS != MPI_Send( &(d[0]),
                                             tot_vsize * sizeof( double ),
                                             MPI_CHAR, 
                                             i,
                                             PM_NEW_PM, 
                                             MPI_COMM_WORLD ) )
               {
                  if ( us_log )
                  {
                     us_log->log( QString( "%1: MPI send failed in best_md0_ga() PM_NEW_PM_DATA\n" ).arg( myrank ) );
                  }
                  MPI_Abort( MPI_COMM_WORLD, errorno - myrank );
                  exit( errorno - myrank );
               }
            }
         }
#endif

         pm.ga_set_params( 
                          control_parameters[ "pmgapopulation"       ].toUInt(),
                          control_parameters[ "pmgagenerations"      ].toUInt(),
                          control_parameters[ "pmgamutate"           ].toDouble(),
                          control_parameters[ "pmgasamutate"         ].toDouble(),
                          control_parameters[ "pmgacrossover"        ].toDouble(),
                          control_parameters[ "pmgaelitism"          ].toUInt(),
                          control_parameters[ "pmgaearlytermination" ].toUInt()
                          );

         pm.set_best_delta(
                           control_parameters[ "pmbestdeltastart"   ].toDouble(),
                           control_parameters[ "pmbestdeltadivisor" ].toDouble(),
                           control_parameters[ "pmbestdeltamin"     ].toDouble()
                           );

         vector < int > types;
         vector < vector < int > > types_vector;

         if ( control_vectors.count( "pmparams" ) )
         {
            params = control_vectors[ "pmparams" ];
         } else {
            if ( control_vectors.count( "pmtypes" ) )
            {
               for ( int i = 0; i < (int) control_vectors[ "pmtypes" ].size(); i++ )
               {
                  types.push_back( (int) control_vectors[ "pmtypes" ][ i ] );
               }
               types_vector.push_back( types );
            } else {
               if ( control_parameters.count( "pmtypes" ) )
               {
                  if ( !pm.expand_types( types_vector
                                         ,control_parameters[ "pmtypes" ]
                                         ,( control_parameters.count( "pmincrementally" ) &&
                                            control_parameters[ "pmincrementally" ] == "on" )
                                         ,( control_parameters.count( "pmallcombinations" ) &&
                                            control_parameters[ "pmallcombinations" ] == "on" ) ) 
                       ||
                       !types_vector.size() )
                  {
                     errormsg = QString( "Error pmbestga : pmtypes string did not expand:" ) + pm.error_msg;
                     return false;
                  }
               } else {
                  errormsg = QString( "Error pmbestga : pmparams or pmtypes must be defined" );
                  return false;
               }
            }
         }

         if ( types_vector.size() )
         {
            if ( !quiet && us_log )
            {
               us_log->log( QString( "total types %1" ).arg( types_vector.size() ) );
            }
            for ( int i = 0; i < (int) types_vector.size(); ++i )
            {
               types = types_vector[ i ];

               pm.zero_params( params, types );

               if ( us_udp_msg )
               {
                  map < QString, QString > msging;
                  msging[ "status" ] = 
                     QString( "start GA for %1%2" )
                     .arg( control_parameters.count( "pmoutname" ) ? control_parameters[ "pmoutname" ] : "unknown" )
                     .arg( pm.get_name( types ) )
                     ;
                  us_udp_msg->send_json( msging );
               }

               if ( !pm.best_ga( 
                                params,
                                model,
                                control_parameters[ "pmgapointsmax"            ].toUInt(),
                                control_parameters[ "pmbestfinestconversion"   ].toDouble(),
                                control_parameters[ "pmbestcoarseconversion"   ].toDouble(),
                                control_parameters[ "pmbestrefinementrangepct" ].toDouble(),
                                control_parameters[ "pmbestconversiondivisor"  ].toDouble()
                                 ) )
               {
                  errormsg = "Error:" + pm.error_msg;
                  return false;
               }

               QString outname = control_parameters[ "pmoutname" ] + pm.get_name( types );

               int ext = 0;
               while ( produced_I.count( outname ) )
               {
                  outname = control_parameters[ "pmoutname" ] + pm.get_name( types ) + QString( "-%1" ).arg( ++ext );
               }
         
               produced_q[ outname ] = pm.q;
               produced_I[ outname ].resize( pm.q.size() );
         
               if ( !pm.qstring_model( produced_model[ outname ], model, params ) )
               {
                  errormsg = QString( "Error producing model %1" ).arg( outname );
                  return false;
               }
            
               if ( !pm.compute_I( model, produced_I[ outname ] ) )
               {
                  errormsg = pm.error_msg;
                  return false;
               }
               if ( us_udp_msg )
               {
                  map < QString, QString > msging;
                  double a = 0e0;
                  double chi2 = 0e0;
                  bool fitok = 
                     pm.use_errors ?
                     scaling_fit( 
                                 produced_I[ outname ],
                                 control_vectors[ "pmi" ],
                                 control_vectors[ "pme" ],
                                 a,
                                 chi2 ) 
                     :
                     scaling_fit( 
                                 produced_I[ outname ],
                                 control_vectors[ "pmi" ],
                                 a,
                                 chi2 )
                     ;
                              
                  msging[ "status" ] = 
                     QString( "end GA for %1%2 model%3" )
                     .arg( control_parameters.count( "pmoutname" ) ? control_parameters[ "pmoutname" ] : "unknown" )
                     .arg( pm.get_name( types ) )
                     .arg( fitok ? 
                           QString( " %1 %2  Params %3  Shannon channels %4" )
                           .arg( pm.use_errors ? "nchi" : "rmsd" )
                           .arg( pm.use_errors ? chi2 / ( control_vectors[ "pmq" ].size() - 1) 
                                 : chi2, 0, 'g', 4 )
                           .arg( params.size() )
                           .arg( pm.last_physical_stats.count( "approx. Shannon channels" ) ? 
                                 pm.last_physical_stats[ "approx. Shannon channels" ] : QString( "?" ) )
                           : QString( "" ) )
                     ;
                  us_udp_msg->send_json( msging );
               }
               

            }
            pm_ga_fitness_secs  += pm.pm_ga_fitness_secs;
            pm_ga_fitness_calls += pm.pm_ga_fitness_calls;

         } else {
            if ( !pm.best_ga( 
                             params,
                             model,
                             control_parameters[ "pmgapointsmax"            ].toUInt(),
                             control_parameters[ "pmbestfinestconversion"   ].toDouble(),
                             control_parameters[ "pmbestcoarseconversion"   ].toDouble(),
                             control_parameters[ "pmbestrefinementrangepct" ].toDouble(),
                             control_parameters[ "pmbestconversiondivisor"  ].toDouble()
                              ) )
            {
               errormsg = "Error:" + pm.error_msg;
               return false;
            }

            pm_ga_fitness_secs  += pm.pm_ga_fitness_secs;
            pm_ga_fitness_calls += pm.pm_ga_fitness_calls;

            QString outname = control_parameters[ "pmoutname" ] + pm.get_name( types );

            int ext = 0;
            while ( produced_I.count( outname ) )
            {
               outname = control_parameters[ "pmoutname" ] + pm.get_name( types ) + QString( "-%1" ).arg( ++ext );
            }
         
            produced_q[ outname ] = pm.q;
            produced_I[ outname ].resize( pm.q.size() );
         
            if ( !pm.qstring_model( produced_model[ outname ], model, params ) )
            {
               errormsg = QString( "Error producing model %1" ).arg( outname );
               return false;
            }
            
            if ( !pm.compute_I( model, produced_I[ outname ] ) )
            {
               errormsg = pm.error_msg;
               return false;
            }
         }
      }         
   }

   return true;
}
