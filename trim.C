#include "trim.h"

#include "functions.h"
#include <iostream>

using namespace MyTRIM_NS;

//#define RANGECORRECT2

//
// all energies are in eV, length unit is Ang
//

// does a single ion cascade
void
TrimBase::trim(IonBase * pka, std::queue<IonBase*> & recoils)
{
  // simconf should already be initialized
  _pka = pka;

  // get length scale
  const Real scale = _simconf->lengthScale();
  const Real invscale = 1.0 / scale;

  // make recoil queue available in overloadable functions
  recoil_queue_ptr = &recoils;

  Real pl = 0.0;

  // maximum electronic energy loss observed
  Real max_dee = 0.0;

  //Real e0kev = _pka->_E / 1000.0;
  int ic = 0;
  unsigned int nn;
  Real r1, r2, hh;
  Real eps, eeg, p, b, r, see;
  Real s2, c2, ct, st;
  Real rr, ex1, ex2, ex3, ex4, v , v1;
  Real fr, fr1, q, roc, sqe;
  Real cc, aa, ff, co, delta;
  Point rdir, perp;
  Real norm, psi;

  Real p1, p2;

  // generate random number for use in the first loop iteration only!
  r1 = _simconf->drand();

  // cycle for each collision
  do
  {
    // increase loop counter
    ++ic;

    // which material is the ion currently in?
    _material = _sample->lookupMaterial(_pka->_pos);
    if (_material == 0)
      break; // TODO: add flight through vacuum

    // normalize direction vector
    v_norm(_pka->_dir);

    // setup max. impact parameter
    eps =  _pka->_E * _material->f;
    eeg = std::sqrt(eps * _material->epsdg); // [TRI02450]
    _material->pmax = _material->a / (eeg + std::sqrt(eeg) + 0.125 * std::pow(eeg, 0.1));

    _ls = 1.0 / (M_PI * Utility::pow<2>(_material->pmax) * _material->_arho); // [TRI02470]
    if (ic == 1)
      _ls = r1 * std::min(_ls, _simconf->cw); // [TRI02480]

    // correct for maximum available range in current _material by increasing maximum impact parameter
    #ifdef RANGECORRECT
      range = _sample->rangeMaterial(_pka->_pos, _pka->_dir) * scale;
      if (range < _ls)
      {
        /* std::cout << "range=" << range << " _ls=" << _ls
              << " pos(0)=" << _pka->_pos(0) << " dir(0)=" << _pka->_dir(0) << '\n';
        std::cout << "CC " << _pka->_pos(0) << ' ' << _pka->_pos(1) << '\n';
        std::cout << "CC " << _pka->_pos(0) + _pka->_dir(0) * range << ' ' << _pka->_pos(1) + _pka->_dir(1) * range << '\n';
        std::cout << "CC " << '\n';*/

        _ls = range;

        // correct pmax to correspond with new _ls
        _material->pmax = 1.0 / std::sqrt(M_PI * _ls * _material->_arho);
      }
    #endif

    // correct for maximum available range in current _material by dropping recoils randomly (faster)
    #ifdef RANGECORRECT2
      range = _sample->rangeMaterial(_pka->_pos, _pka->_dir) * scale;
      if (range < _ls)
      {
        // skip this recoil, just advance the ion
        if (range / _ls < _simconf->drand())
        {
          // electronic stopping
          _pka->_E -= range * _material->getrstop(_pka);

          // free flight
          _pka->_pos += _pka->_dir * range;

          // start over
          continue;
        }
        _ls = range;
      }
    #endif

    // advance clock pathlength/velocity
    // time in fs! m in u, l in Ang, e in eV
    // 1000g/kg, 6.022e23/mol, 1.602e-19J/eV, 1e5m/s=1Ang/fs 1.0/0.09822038

    // requires IonClock PKAs. We could do a dynamic_cast here but it might be too slow...
    //_pka->_time += 10.1811859 * (_ls - _simconf->tau) / std::sqrt(2.0 * _pka->_E / _pka->_m);

    // choose impact parameter
    r2 = _simconf->drand();
    p = _material->pmax * std::sqrt(r2);

    // which atom in the _material will be hit
    hh = _simconf->drand(); // selects _element inside _material to scatter from
    const unsigned int end = _material->_element.size();
    for (nn = 0; nn < end; ++nn)
    {
      hh -= _material->_element[nn]._t;
      if (hh <= 0) break;
    }

    _element = &(_material->getElement(nn));

    // epsilon and reduced impact parameter b
    eps = _element->fi * _pka->_E;
    b = p / _element->ai;

    // ie = int(_pka.e / e0kev - 0.5); // was +0.5 for fortran indices
    // ie = int(_pka.e / _material->semax - 0.5); // was +0.5 for fortran indices
    // see = _material->se[ie];

    see = _material->getrstop(_pka);
    //if (_pka.e < e0kev) see = _material->se[0] * std::sqrt(_pka.e / e0kev);

    // calculate electronic energy loss along the path segment _ls
    _dee = _ls * see;

    if (eps > 10.0)
    {
      // use Rutherford scattering [TRI02690]
      s2 = 1.0 / (1.0 + (1.0 + b * (1.0 + b)) * Utility::pow<2>(2.0 * eps * b));
      c2 = 1.0 - s2;
      ct = 2.0 * c2 - 1.0;
      st = std::sqrt(1.0 - ct*ct);
    }
    else
    {
      // first guess at ion c.p.a. [TRI02780]
      r = b;
      rr = -2.7 * std::log(eps * b);
      if (rr >= b)
      {
        rr = -2.7 * std::log(eps * rr);
        if (rr >= b) r = rr;
      }

      do
      {
        switch (_potential)
        {
          case UNIVERSAL:
            // universal potential
            ex1 = 0.18175 * std::exp(-3.1998 * r);
            ex2 = 0.50986 * std::exp(-0.94229 * r);
            ex3 = 0.28022 * std::exp(-0.4029 * r);
            ex4 = 0.028171 * std::exp(-0.20162 * r);
            v = (ex1 + ex2 + ex3 + ex4) / r;
            v1 = -(v + 3.1998 *ex1 + 0.94229 * ex2 + 0.4029 * ex3 + 0.20162 * ex4) / r;
            break;

          case MOLIERE:
            // Moliere potential
            ex1 = std::exp(-0.3 * r);
            ex2 = Utility::pow<4>(ex1);
            ex3 = Utility::pow<5>(ex2);
            v = (0.35 * ex1 + 0.55 * ex2 + 0.1 * ex3) / r;
            v1 = -(v + 0.105 * ex1 + 0.66 * ex2 + 0.6 * ex3) / r;
            break;

          case CKR:
            // C-Kr potential
            ex1 = std::exp(-0.279 * r);
            ex2 = std::exp(-0.637 * r);
            ex3 = std::exp(-1.1919 * r);
            v = (0.191 * ex1 + 0.474 * ex2 + 0.335 * ex3) / r;
            v1 = -(v + 0.531865 * ex1 + 0.30181 * ex2 + 0.6437 * ex3) / r;
            break;

          default:
            std::cerr << "Invalid potential" << std::endl;
            exit(1);
        }

        fr = b*b / r + v * r / eps -r;
        fr1 = - b*b / (r*r) + (v + v1 * r) / eps - 1.0;
        q = fr / fr1;
        r -= q;
      } while (std::abs(q/r) > 0.001); // [TRI03110]

      roc = -2.0 * (eps - v) / v1;
      sqe = std::sqrt(eps);

      switch (_potential)
      {
        case UNIVERSAL:
          // 5-parameter magic scattering calculation (universal pot.)
          cc = (0.011615 + sqe) / (0.0071222 + sqe);                  // 2-87 beta
          aa = 2.0 * eps * (1.0 + (0.99229 / sqe)) * std::pow(b, cc); // 2-87 A
          ff = (std::sqrt(aa*aa + 1.0) - aa) * ((9.3066 + eps) / (14.813 + eps));
          break;

        case MOLIERE:
          // Moliere potential
          cc = (0.009611 + sqe) / (0.005175 + sqe);
          aa = 2.0 * eps * (1.0 + (0.6743 / sqe)) * std::pow(b, cc);
          ff = (std::sqrt(aa*aa + 1.0) - aa) * ((6.314 + eps) / (10.0 + eps));
          break;

        case CKR:
          // C-Kr potential
          cc = (0.235809 + sqe) / (0.126000 + sqe);
          aa = 2.0 * eps * (1.0 + (1.0144 / sqe)) * std::pow(b, cc);
          ff = (std::sqrt(aa*aa + 1.0) - aa) * ((6935.0 + eps) / (83550.0 + eps));
          break;

        default:
          std::cerr << "Invalid potential" << std::endl;
          exit(1);
      }

      delta = (r - b) * aa * ff / (ff + 1.0);
      co = (b + delta + roc) / (r + roc);
      c2 = co*co;
      s2 = 1.0 - c2;
      ct = 2.0 * c2 - 1.0;
      st = std::sqrt(1.0 - ct*ct);
    } // end non-Rutherford scattering

    // energy transferred to recoil atom [TRI03350]
    _den = _element->ec * s2 * _pka->_E;

    if (_dee > _pka->_E) {
      // avoid getting negative energies
      _dee = _pka->_E;

      // sanity check
      if (_den > 100.0)
        std::cerr << " electronic energy loss stopped the ion. Broken _recoil!!\n";
    }

    // electronic energy loss
    _pka->_E -= _dee;
    _simconf->EelTotal += _dee;

    // momentum transfer
    p1 = std::sqrt(2.0 * _pka->_m * _pka->_E); // momentum before collision
    if (_den > _pka->_E) _den = _pka->_E; // avoid negative energy
    _pka->_E -= _den;
    p2 = std::sqrt(2.0 * _pka->_m * _pka->_E); // momentum after collision

    // track maximum electronic energy loss TODO: might want to track max(see)!
    if (_dee > max_dee)
      max_dee = _dee;

    // total path lenght
    pl += _ls - _simconf->tau;

    // find new position, save old direction to recoil
    _recoil = _pka->spawnRecoil();

    // used to assign the new position to the recoil, but
    // we have to make sure the recoil starts in the appropriate _material!
    _pka->_pos += _pka->_dir * (_ls - _simconf->tau) * invscale;
    _recoil->_dir = _pka->_dir * p1;

    _recoil->_E = _den;

    // recoil loses the lattice binding energy
    _recoil->_E -= _element->_Elbind;
    _recoil->_m = _element->_m;
    _recoil->_Z = _element->_Z;

    // create a random vector perpendicular to _pka.dir
    // there is a cleverer way by using the azimuthal angle of scatter...
    do
    {
      do
      {
        for (unsigned int i = 0; i < 3; ++i)
          rdir(i) = 2.0 * _simconf->drand() - 1.0;
      }
      while (rdir.norm_sq() > 1.0);

      v_cross(_pka->_dir, rdir, perp);
      norm = perp.norm();
    }
    while (norm == 0.0);
    perp /= norm;

    // PKA scattering angle
    psi = std::atan2(st, ct + _element->my);

    // calculate new direction, subtract from old dir (stored in recoil)
    _pka->_dir *= std::cos(psi);
    _pka->_dir += perp * std::sin(psi);
    _recoil->_dir -= _pka->_dir * p2;

    // end cascade if a CUT boundary is crossed
    for (unsigned int i = 0; i < 3; ++i)
    {
      if (_sample->bc[i] == SampleBase::CUT &&
           (_pka->_pos(i) > _sample->w[i] || _pka->_pos(i)<0.0))
      {
        _pka->_state = IonBase::LOST;
        break;
      }
    }

    //
    // decide on the fate of recoil and _pka
    //
    if (_pka->_state != IonBase::LOST) {
      if (_recoil->_E > _element->_Edisp) {
        // non-physics based descision on recoil following
        if (followRecoil()) {
          v_norm(_recoil->_dir);
          _recoil->_tag = _material->_tag;
          _recoil->_id  = _simconf->_id++;

          // queue recoil for processing
          recoils.push(_recoil);
          if (_simconf->fullTraj)
            std::cout << "spawn " << _recoil->_id << ' ' << _pka->_id << '\n';
        } else {
          // this recoil could have left its lattice site, but we chose
          // not to follow it (simulation of PKAs only)
          _recoil->_id = IonBase::DELETE;
        }

        // will the knock-on get trapped at the recoil atom site?
        // (TODO: make sure that _pka->_Ef < _element->_Edisp for all elements!)
        // did we create a vacancy by knocking out the recoil atom?
        if (_pka->_E > _element->_Edisp) {
          // yes, because the knock-on can escape, too!
          vacancyCreation();
        } else {
          // nope, the _pka gets stuck at that site as...
          replacementCollision();
          if (_pka->_Z == _element->_Z)
            _pka->_state = IonBase::REPLACEMENT;
          else
            _pka->_state = IonBase::SUBSTITUTIONAL;
        }
      } else {
        // this recoil will not leave its lattice site
        dissipateRecoilEnergy();
        _recoil->_id  = IonBase::DELETE;;

        // if the PKA has no energy left, put it to rest here as an interstitial
        if (_pka->_E < _pka->_Ef) {
          _pka->_state = IonBase::INTERSTITIAL;
        }
      }
    }

    // delete recoil if it was not queued
    if (_recoil->_id  == IonBase::DELETE)
      delete _recoil;

    // act on the _pka state change
    checkPKAState();

    // output the full trajectory (state is not output by the ion object)
    if (_simconf->fullTraj)
      std::cout << _pka->_state << ' ' << *_pka << '\n';

  } while (_pka->_state == IonBase::MOVING);
}

bool
TrimBase::followRecoil()
{
  // TODO: find a better place for this!
  /*if (_pka->_md > 0)
    _recoil->_md = _pka->_md +1;
  else
    _recoil->_md = 0;
  */
  return true;
}

void
TrimBase::vacancyCreation()
{
  _simconf->vacancies_created++;
}

void
TrimPrimaries::vacancyCreation()
{
  _simconf->vacancies_created++;

  // Modified Kinchin-Pease
  if (_recoil->_gen == maxGen())
  {
    // calculate modified kinchin pease data
    // http://www.iue.tuwien.ac.at/phd/hoessinger/node47.html
    Real ed = 0.0115 * std::pow(_material->_az, -7.0/3.0) * _recoil->_E;
    Real g = 3.4008 * std::pow(ed, 1.0/6.0) + 0.40244 * std::pow(ed, 3.0/4.0) + ed;
    Real kd = 0.1337 * std::pow(_material->_az, 2.0/3.0) / std::sqrt(_material->_am); //Z, M
    Real Ev = _recoil->_E / (1.0 + kd * g);
    _simconf->vacancies_created += int(0.8 * Ev / (2.0 * _element->_Edisp));

    // TODO: this is missing the energy threshold of 2.5Ed!!!!
    // TODO: should be something like _material->_Edisp (average?)
  }
}

void
TrimDefectLog::vacancyCreation()
{
  _os << "V " << *_recoil << '\n';
}

void
TrimDefectLog::checkPKAState()
{
  if (_pka->_state == IonBase::INTERSTITIAL)
    _os << "I " << *_pka << '\n';
  else if (_pka->_state == IonBase::SUBSTITUTIONAL)
    _os << "S " << *_pka << '\n';
  else if (_pka->_state == IonBase::REPLACEMENT)
    _os << "R " << *_pka << '\n';
}

void TrimVacMap::vacancyCreation()
{
  // both atoms have enough energy to leave the site
  int x, y;

  x = ((_recoil->_pos(0) * mx) / _sample->w[0]);
  y = ((_recoil->_pos(1) * my) / _sample->w[1]);
  x -= int(x/mx) * mx;
  y -= int(y/my) * my;

  // keep track of vaccancies for the two constituents
  if (_recoil->_Z == _z1)
    vmap[x][y][0]++;
  else if (_recoil->_Z == _z2)
    vmap[x][y][1]++;
  else if (_recoil->_Z == _z3)
    vmap[x][y][2]++;
}

void
TrimPhononOut::checkPKAState()
{
  if (_pka->_state == IonBase::MOVING ||
      _pka->_state == IonBase::LOST) return;

  _os << _pka->_E << ' ' <<  *_pka << '\n';
  _simconf->EnucTotal += _pka->_E;
}

void
TrimPhononOut::dissipateRecoilEnergy()
{
  Real Edep = _recoil->_E + _element->_Elbind;
  _os << Edep << ' ' <<  *_recoil << '\n';
  _simconf->EnucTotal += Edep;
}

bool
TrimPhononOut::followRecoil()
{
  _os << _element->_Elbind << ' ' <<  *_recoil << '\n';
  _simconf->EnucTotal += _element->_Elbind;
  return true;
}
