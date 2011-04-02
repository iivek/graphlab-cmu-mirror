

#ifndef GRAPHLAB_MASTER_INCLUDES
#define GRAPHLAB_MASTER_INCLUDES



//

#include <graphlab/engine/engine_includes.hpp>
#include <graphlab/factors/factor_includes.hpp>
#include <graphlab/graph/graph_includes.hpp>
#include <graphlab/logger/logger_includes.hpp>
#include <graphlab/monitoring/monitoring_includes.hpp>
#include <graphlab/parallel/parallel_includes.hpp>
#include <graphlab/schedulers/scheduler_includes.hpp>
#include <graphlab/scope/scope_includes.hpp>
#include <graphlab/serialization/serialization_includes.hpp>
#include <graphlab/shared_data/shared_data_includes.hpp>
#include <graphlab/tasks/task_includes.hpp>
#include <graphlab/util/util_includes.hpp>

#include <graphlab/core.hpp>


#ifdef GLDISTRIBUTED
#include <graphlab/distributed/distributed_includes.hpp>
#include <graphlab/distributed/distributed_engine.hpp>
#endif 



/**
   \namespace graphlab
   \brief The namespace containing all graphlab objects and functions.

   All objects and functions used in graphlab are contained within the
   graphlab namespace.  Forexample to access the graph type a user
   must therefore either include the graphlab namespace or use:

   <code>
     graphlab::graph<VertexType, EdgeType>
   </code>

   Because most of the graphlab types depend on the graph type we have
   created a templated struct called graphlab::types. \todo finish
   explanation.
                           
*/
namespace graphlab {
 
  /**
  \brief A types datastructure which provides convenient specializations of all
  user-facing GraphLab types.
  
  GraphLab is heavily templatized. The graphlab::types object provides a 
  convenient way to access the GraphLab classes without requiring excessive 
  angle brackets (< , >). The GraphLab types object is located in <graphlab.hpp>. 
  To define a graphlab type object:
  
  \code
  typedef graphlab::graph<vertex_data, edge_data> graph_type;
  typedef graphlab::types<graph_type> gl;
  \endcode
  
  Now we can use gl::... to access all the available graphlab types. 
  */
  template<typename Graph>
  struct types {
    ///  \brief The type of the Graph. 
    typedef Graph graph;

    /** \brief A convenient wrapper object around the commonly used
    portions of GraphLab.  This is useful for most GraphLab
    applications. See the \ref graphlab::core object for more details.
    */
    typedef graphlab::core<typename graph::vertex_data_type,
                           typename graph::edge_data_type> core;

    typedef graphlab::command_line_options command_line_options;
    typedef graphlab::engine_options engine_options;
    
    /// \brief The type of the data stored on each vertex of the Graph. 
    typedef typename graph::vertex_data_type vertex_data_type;
    
    /// \brief The type of the data stored on each edge of the Graph.   
    typedef typename graph::edge_data_type   edge_data_type;
    
    typedef graphlab::update_task<graph>        update_task;
    typedef typename update_task::update_function_type update_function;
    
    typedef graphlab::iscope<graph>              iscope;
    typedef graphlab::ischeduler<graph>          ischeduler;
    typedef graphlab::icallback<graph>           icallback;
    typedef graphlab::iengine<graph>             iengine;
    typedef graphlab::imonitor<graph>            imonitor;

    typedef graphlab::ishared_data<graph>        ishared_data;
    typedef graphlab::ishared_data_manager<graph> ishared_data_manager;
    typedef graphlab::sync_ops<Graph> sync_ops;
    typedef graphlab::apply_ops<Graph> apply_ops;
    typedef graphlab::glshared_sync_ops<Graph> glshared_sync_ops;
    typedef graphlab::glshared_apply_ops glshared_apply_ops;

    typedef graphlab::thread_shared_data<graph>  thread_shared_data;



    template<typename Scheduler, typename ScopeFactory>
    struct engines {
      typedef graphlab::
      asynchronous_engine<graph, Scheduler, ScopeFactory> asynchronous;
      #ifdef GLDISTRIBUTED
      typedef graphlab::distributed_engine<graph, Scheduler> distributed;
      #endif
    };
    

    typedef graphlab::fifo_scheduler<graph> fifo_scheduler;
    typedef graphlab::priority_scheduler<graph> priority_scheduler;
    typedef graphlab::sampling_scheduler<graph> sampling_scheduler;
    typedef graphlab::sweep_scheduler<graph> sweep_scheduler;
    typedef graphlab::multiqueue_fifo_scheduler<graph> multiqueue_fifo_scheduler;
    typedef graphlab::multiqueue_priority_scheduler<graph> multiqueue_priority_scheduler;
    typedef graphlab::clustered_priority_scheduler<graph> clustered_priority_scheduler;
    typedef graphlab::round_robin_scheduler<graph> round_robin_scheduler;
    typedef graphlab::chromatic_scheduler<graph> chromatic_scheduler;
    
    
    
    
    

    /// \brief The type of id assigned to each vertex. Equivalent to graphlab::vertex_id_t
    typedef graphlab::vertex_id_t vertex_id_t;
    /// \brief The type of id assigned to each vertex. Equivalent to graphlab::edge_id_t
    typedef graphlab::edge_id_t edge_id_t;

    typedef typename graph::edge_list_type edge_list;
    
    typedef graphlab::scheduler_options          scheduler_options;
    typedef graphlab::sched_status               sched_status;
    typedef graphlab::partition_method           partition_method;
    typedef graphlab::scope_range scope_range;

    typedef graphlab::random  random;

    template <typename T>
    class glshared:public graphlab::glshared<T> { };
  };

}


#endif